#pragma once
#include <3ds.h>

// 3DS system shared font renderer (CTRPF-style: 16px line, anti-aliased).
// Draws into an RGB888 compose buffer (400x240, 3 bytes/px, row-major).

int  SysFontInit(void);   // 0 = ok (safe to call multiple times)
int  SysFontReady(void);
int  SysFontError(void);  // last init status code (diagnostics)

// Returns the horizontal advance used. bold=1 doubles pixels horizontally.
int  SysFontDrawChar(u8 *cb, int x, int y, unsigned char c, u8 r, u8 g, u8 b, int bold);
int  SysFontDrawText(u8 *cb, int x, int y, const char *s, u8 r, u8 g, u8 b, int bold);
int  SysFontTextWidth(const char *s);

// Decode one UTF-8 sequence at *ps; returns the codepoint and advances *ps.
u32  SysFontUtf8Next(const char **ps);
