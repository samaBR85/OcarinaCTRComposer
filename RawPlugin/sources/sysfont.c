// System shared-font renderer for a raw Luma plugin.
//
// How CTRPF gets its pretty text: the 3DS keeps a shared system font (BCFNT)
// in a shared-memory block. The GAME (OoT3D) maps it at boot via
// APT_GetSharedFont + svcMapMemoryBlock, so by the time our menu opens the
// font is already mapped in this process. We only need to ask APT for its
// address (raw IPC, no aptInit), then decode the glyph sheets (8x8-tiled
// 4bit-alpha textures), shrink 2x with a box filter and alpha-blend.
// This mirrors CTRPluginFrameworkImpl/Graphics/FontImpl.cpp.

#include <3ds.h>
#include <string.h>
#include "sysfont.h"

#define GLYPH_W   13   // cached glyph size (25x32 cell shrunk by 2)
#define GLYPH_H   16
#define ASCII_LO  0x20
#define ASCII_HI  0x7E
#define NGLYPHS   (ASCII_HI - ASCII_LO + 1)

static CFNT_s  *sFont;                       // shared font (header + 0x80)
static int      sReady;
static int      sErr = 99;                   // last init status (for on-screen diag)
static u8       sCache[NGLYPHS][GLYPH_W * GLYPH_H];
static s8       sXOff[NGLYPHS];
static u8       sXAdv[NGLYPHS];

// ---------- extended (non-ASCII) glyph cache: accented Latin for FR/DE/IT/ES/PT ----------
// Codepoints beyond ASCII used by the 6 Latin UI languages. The 3DS shared BCFNT
// contains them all; we pre-cache them the same way as ASCII so localized menu
// text (Inventário, Français, Größe, Configuración) renders with real accents.
static const u16 kExtCodes[] = {
    // uppercase
    0x00C0,0x00C1,0x00C2,0x00C3,0x00C4,0x00C5,0x00C7,0x00C8,0x00C9,0x00CA,0x00CB,
    0x00CC,0x00CD,0x00CE,0x00CF,0x00D1,0x00D2,0x00D3,0x00D4,0x00D5,0x00D6,
    0x00D9,0x00DA,0x00DB,0x00DC,0x00DF,
    // lowercase
    0x00E0,0x00E1,0x00E2,0x00E3,0x00E4,0x00E5,0x00E7,0x00E8,0x00E9,0x00EA,0x00EB,
    0x00EC,0x00ED,0x00EE,0x00EF,0x00F1,0x00F2,0x00F3,0x00F4,0x00F5,0x00F6,
    0x00F9,0x00FA,0x00FB,0x00FC,
    // punctuation used by ES/FR/PT
    0x00A1,0x00BF,0x00AA,0x00BA,0x00AB,0x00BB,
};
#define NEXT ((int)(sizeof(kExtCodes)/sizeof(kExtCodes[0])))

static u8  sExtCache[NEXT][GLYPH_W * GLYPH_H];
static s8  sExtXOff[NEXT];
static u8  sExtXAdv[NEXT];
static int sExtReady[NEXT];   // 1 = glyph present in font and cached

// Decode one UTF-8 sequence at *ps, return codepoint, advance *ps past it.
// Malformed / truncated input degrades gracefully (returns the raw byte).
u32 SysFontUtf8Next(const char **ps)
{
    const unsigned char *p = (const unsigned char *)*ps;
    u32 c = *p++;
    if (c >= 0x80)
    {
        if      ((c & 0xE0) == 0xC0 && (p[0] & 0xC0) == 0x80)
        { c = ((c & 0x1F) << 6) | (p[0] & 0x3F); p += 1; }
        else if ((c & 0xF0) == 0xE0 && (p[0] & 0xC0) == 0x80 && (p[1] & 0xC0) == 0x80)
        { c = ((c & 0x0F) << 12) | ((p[0] & 0x3F) << 6) | (p[1] & 0x3F); p += 2; }
        else if ((c & 0xF8) == 0xF0 && (p[0] & 0xC0) == 0x80 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80)
        { c = ((c & 0x07) << 18) | ((p[0] & 0x3F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); p += 3; }
        // else: leave c as the lone byte (invalid continuation) and don't advance further
    }
    *ps = (const char *)p;
    return c;
}

static int ExtSlot(u32 code)
{
    for (int i = 0; i < NEXT; ++i)
        if (kExtCodes[i] == code) return i;
    return -1;
}

// ---------- raw APT:GetSharedFont (no aptInit — game already did the setup) ----------
static Result AptGetSharedFont(u32 *mapAddr, Handle *blockHandle)
{
    static const char *names[] = { "APT:U", "APT:A", "APT:S" };
    Handle apt = 0;
    Result res = -1;

    for (int i = 0; i < 3 && R_FAILED(res); ++i)
        res = srvGetServiceHandle(&apt, names[i]);
    if (R_FAILED(res)) return res;

    u32 *cmdbuf = getThreadCommandBuffer();
    cmdbuf[0] = IPC_MakeHeader(0x44, 0, 0); // APT_GetSharedFont
    res = svcSendSyncRequest(apt);
    if (R_SUCCEEDED(res)) res = (Result)cmdbuf[1];
    if (R_SUCCEEDED(res))
    {
        *mapAddr     = cmdbuf[2];
        *blockHandle = cmdbuf[4];
    }
    svcCloseHandle(apt);
    return res;
}

static int AddrReadable(u32 addr)
{
    MemInfo  info;
    PageInfo page;
    if (R_FAILED(svcQueryMemory(&info, &page, addr))) return 0;
    return (info.perm & MEMPERM_READ) != 0;
}

// ---------- BCFNT lookups (own walkers: no libctru font calls, no surprises) ----------
static int GlyphIndexFromCode(u32 code)
{
    for (CMAP_s *cmap = sFont->finf.cmap; cmap; cmap = cmap->next)
    {
        if (code < cmap->codeBegin || code > cmap->codeEnd) continue;
        switch (cmap->mappingMethod)
        {
            case CMAP_TYPE_DIRECT:
                return cmap->indexOffset + (code - cmap->codeBegin);
            case CMAP_TYPE_TABLE:
            {
                int idx = cmap->indexTable[code - cmap->codeBegin];
                return (idx == 0xFFFF) ? -1 : idx;
            }
            case CMAP_TYPE_SCAN:
                for (int i = 0; i < cmap->nScanEntries; ++i)
                    if (cmap->scanEntries[i].code == code)
                        return cmap->scanEntries[i].glyphIndex;
                return -1;
        }
    }
    return -1;
}

static charWidthInfo_s *CharWidthInfo(int glyphIndex)
{
    for (CWDH_s *cwdh = sFont->finf.cwdh; cwdh; cwdh = cwdh->next)
        if (glyphIndex >= cwdh->startIndex && glyphIndex <= cwdh->endIndex)
            return &cwdh->widths[glyphIndex - cwdh->startIndex];
    return NULL;
}

// ---------- sheet decoding (port of CTRPF's GetOriginalGlyph, tile-clamped) ----------
static u8 sRaw[26 * 32]; // one decoded cell, stride = sRawStride
static int sRawStride;

static inline u8 SheetAlpha(const u8 *data, int pos, int fmt)
{
    if (fmt == 11) // GPU_A4
    {
        u8 byte = data[pos >> 1];
        return (u8)(((byte >> ((pos & 1) * 4)) & 0x0F) * 0x11);
    }
    if (fmt == 8)  // GPU_A8
        return data[pos];
    return 0;
}

static void DecodeGlyphCell(int glyphIndex)
{
    TGLP_s *tglp = sFont->finf.tglp;
    int perSheet = tglp->nRows * tglp->nLines;
    const u8 *data = &tglp->sheetData[tglp->sheetSize * (glyphIndex / perSheet)];
    int index = glyphIndex % perSheet;

    int dataWidth  = tglp->sheetWidth;
    int dataHeight = tglp->sheetHeight;

    // pow2-align like CTRPF (sheet sizes are normally pow2 already)
    int width = 1;  while (width  < dataWidth)  width  <<= 1;
    int height = 1; while (height < dataHeight) height <<= 1;

    int indexX = index % tglp->nRows;
    int indexY = index / tglp->nRows;
    int singleWx = width  / tglp->nRows;
    int singleHy = height / tglp->nLines;
    int startPx = indexX * singleWx, endPx = startPx + singleWx;
    int startPy = indexY * singleHy, endPy = startPy + singleHy;

    sRawStride = singleWx;
    if (sRawStride > 26) sRawStride = 26;
    memset(sRaw, 0, sizeof(sRaw));

    // Only visit the 8x8 tiles overlapping our cell (CTRPF scans the whole sheet)
    int t0x = startPx / 8, t1x = (endPx + 7) / 8;
    int t0y = startPy / 8, t1y = (endPy + 7) / 8;
    if (t1x > width / 8)  t1x = width / 8;
    if (t1y > height / 8) t1y = height / 8;

    for (int tileY = t0y; tileY < t1y; tileY++)
    for (int tileX = t0x; tileX < t1x; tileX++)
    for (int y = 0; y < 2; y++)
    for (int x = 0; x < 2; x++)
    for (int yy = 0; yy < 2; yy++)
    for (int xx = 0; xx < 2; xx++)
    for (int yyy = 0; yyy < 2; yyy++)
    for (int xxx = 0; xxx < 2; xxx++)
    {
        int pixelX = xxx + xx * 2 + x * 4 + tileX * 8;
        int pixelY = yyy + yy * 2 + y * 4 + tileY * 8;
        if (pixelX >= dataWidth || pixelY >= dataHeight) continue;
        if (pixelX < startPx || pixelX >= endPx) continue;
        if (pixelY < startPy || pixelY >= endPy) continue;

        int dataX = xxx + xx * 4 + x * 16 + tileX * 64;
        int dataY = yyy * 2 + yy * 8 + y * 32 + tileY * width * 8;

        int gx = pixelX - startPx, gy = pixelY - startPy;
        if (gx < sRawStride && gy < 32)
            sRaw[gy * sRawStride + gx] = SheetAlpha(data, dataX + dataY, tglp->sheetFmt);
    }
}

// 2x box-filter shrink (what CTRPF's ShrinkGlyph reduces to for the system font)
static void ShrinkInto(u8 *dst)
{
    for (int y = 0; y < GLYPH_H; ++y)
        for (int x = 0; x < GLYPH_W; ++x)
        {
            int sx = x * 2, sy = y * 2, sum = 0, n = 0;
            for (int dy = 0; dy < 2; ++dy)
                for (int dx = 0; dx < 2; ++dx)
                    if (sx + dx < sRawStride && sy + dy < 32)
                        { sum += sRaw[(sy + dy) * sRawStride + sx + dx]; n++; }
            dst[y * GLYPH_W + x] = n ? (u8)(sum / n) : 0;
        }
}

// ---------- init: map/locate font, pre-cache ASCII ----------
static int SysFontInitInternal(void)
{
    if (sReady) return 0;

    u32    addr = 0;
    Handle block = 0;
    if (R_FAILED(AptGetSharedFont(&addr, &block)) || !addr) return -1;

    // Map at addr 0: the block carries its own fixed address (libctru does the
    // same). 0xE0A01BF5 = already mapped (e.g. by the game) — that's fine too.
    if (block)
    {
        Result mres = svcMapMemoryBlock(block, 0, MEMPERM_READ, MEMPERM_DONTCARE);
        svcCloseHandle(block);
        if (R_FAILED(mres) && mres != (Result)0xE0A01BF5 && !AddrReadable(addr))
            return -2;
    }
    if (!AddrReadable(addr)) return -3;

    CFNT_s *font = (CFNT_s *)(addr + 0x80);
    if (font->signature != 0x544E4643 &&  // 'CFNT'
        font->signature != 0x554E4643)    // 'CFNU' (decompressed shared font)
        return -4;
    sFont = font;

    for (int i = 0; i < NGLYPHS; ++i)
    {
        int gi = GlyphIndexFromCode((u32)(ASCII_LO + i));
        charWidthInfo_s *cwi = (gi >= 0) ? CharWidthInfo(gi) : NULL;
        if (!cwi) { memset(sCache[i], 0, sizeof(sCache[i])); sXOff[i] = 0; sXAdv[i] = 6; continue; }

        DecodeGlyphCell(gi);
        ShrinkInto(sCache[i]);
        sXOff[i] = (s8)((cwi->left + 1) / 2);        // 0.5x scale, rounded
        sXAdv[i] = (u8)((cwi->charWidth + 1) / 2);
    }

    // Pre-cache the accented Latin set the same way. Missing glyphs stay unready
    // and fall back to a stripped base letter at draw time.
    for (int i = 0; i < NEXT; ++i)
    {
        int gi = GlyphIndexFromCode((u32)kExtCodes[i]);
        charWidthInfo_s *cwi = (gi >= 0) ? CharWidthInfo(gi) : NULL;
        if (!cwi) { sExtReady[i] = 0; continue; }
        DecodeGlyphCell(gi);
        ShrinkInto(sExtCache[i]);
        sExtXOff[i] = (s8)((cwi->left + 1) / 2);
        sExtXAdv[i] = (u8)((cwi->charWidth + 1) / 2);
        sExtReady[i] = 1;
    }

    sReady = 1;
    return 0;
}

int SysFontInit(void)
{
    sErr = SysFontInitInternal();
    return sErr;
}

int SysFontReady(void) { return sReady; }
int SysFontError(void) { return sErr; }

// Base ASCII letter for an accented codepoint (used only if the font somehow
// lacks the accented glyph — normally the real accented glyph is cached).
static unsigned char AccentBaseAscii(u32 code)
{
    switch (code)
    {
        case 0x00C0: case 0x00C1: case 0x00C2: case 0x00C3: case 0x00C4: case 0x00C5: return 'A';
        case 0x00C7: return 'C';
        case 0x00C8: case 0x00C9: case 0x00CA: case 0x00CB: return 'E';
        case 0x00CC: case 0x00CD: case 0x00CE: case 0x00CF: return 'I';
        case 0x00D1: return 'N';
        case 0x00D2: case 0x00D3: case 0x00D4: case 0x00D5: case 0x00D6: return 'O';
        case 0x00D9: case 0x00DA: case 0x00DB: case 0x00DC: return 'U';
        case 0x00DF: return 's';
        case 0x00E0: case 0x00E1: case 0x00E2: case 0x00E3: case 0x00E4: case 0x00E5: return 'a';
        case 0x00E7: return 'c';
        case 0x00E8: case 0x00E9: case 0x00EA: case 0x00EB: return 'e';
        case 0x00EC: case 0x00ED: case 0x00EE: case 0x00EF: return 'i';
        case 0x00F1: return 'n';
        case 0x00F2: case 0x00F3: case 0x00F4: case 0x00F5: case 0x00F6: return 'o';
        case 0x00F9: case 0x00FA: case 0x00FB: case 0x00FC: return 'u';
        case 0x00AA: return 'a';
        case 0x00BA: return 'o';
        case 0x00A1: return '!';
        case 0x00BF: return '?';
        default: return 0;
    }
}

// Resolve a codepoint to a cached glyph (data + metrics). Returns 0 if nothing
// drawable. Handles ASCII, cached accented glyphs, and accent->base fallback.
static int GlyphFor(u32 code, const u8 **gd, s8 *xoff, u8 *xadv)
{
    if (code >= ASCII_LO && code <= ASCII_HI)
    {
        int ci = (int)code - ASCII_LO;
        *gd = sCache[ci]; *xoff = sXOff[ci]; *xadv = sXAdv[ci];
        return 1;
    }
    int e = ExtSlot(code);
    if (e >= 0 && sExtReady[e])
    {
        *gd = sExtCache[e]; *xoff = sExtXOff[e]; *xadv = sExtXAdv[e];
        return 1;
    }
    unsigned char base = AccentBaseAscii(code);
    if (base >= ASCII_LO && base <= ASCII_HI)
    {
        int ci = (int)base - ASCII_LO;
        *gd = sCache[ci]; *xoff = sXOff[ci]; *xadv = sXAdv[ci];
        return 1;
    }
    return 0;
}

static int DrawGlyph(u8 *cb, int x, int y, const u8 *gd, s8 xoff, u8 xadv, u8 r, u8 g, u8 b, int bold)
{
    int px = x + xoff;
    for (int gy = 0; gy < GLYPH_H; ++gy)
        for (int gx = 0; gx < GLYPH_W; ++gx)
        {
            u32 a = gd[gy * GLYPH_W + gx];
            if (a <= 12) continue; // skip barely-visible pixels (CTRPF does too)
            int reps = bold ? 2 : 1;
            for (int j = 0; j < reps; ++j)
            {
                int X = px + gx + j, Y = y + gy;
                if ((unsigned)X >= 400 || (unsigned)Y >= 240) continue;
                u8 *p = &cb[(Y * 400 + X) * 3];
                p[0] = (u8)((p[0] * (255 - a) + r * a) / 255);
                p[1] = (u8)((p[1] * (255 - a) + g * a) / 255);
                p[2] = (u8)((p[2] * (255 - a) + b * a) / 255);
            }
        }
    return xoff + xadv;
}

int SysFontDrawChar(u8 *cb, int x, int y, unsigned char c, u8 r, u8 g, u8 b, int bold)
{
    const u8 *gd; s8 xoff; u8 xadv;
    if (!GlyphFor((u32)c, &gd, &xoff, &xadv)) return 0;
    return DrawGlyph(cb, x, y, gd, xoff, xadv, r, g, b, bold);
}

int SysFontDrawText(u8 *cb, int x, int y, const char *s, u8 r, u8 g, u8 b, int bold)
{
    int x0 = x;
    while (*s)
    {
        u32 code = SysFontUtf8Next(&s);
        const u8 *gd; s8 xoff; u8 xadv;
        if (GlyphFor(code, &gd, &xoff, &xadv))
            x += DrawGlyph(cb, x, y, gd, xoff, xadv, r, g, b, bold);
    }
    return x - x0;
}

int SysFontTextWidth(const char *s)
{
    int w = 0;
    while (*s)
    {
        u32 code = SysFontUtf8Next(&s);
        const u8 *gd; s8 xoff; u8 xadv;
        if (GlyphFor(code, &gd, &xoff, &xadv))
            w += xoff + xadv;
    }
    return w;
}
