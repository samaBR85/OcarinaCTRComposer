// ===================== Checklist 100% (progression-ordered, mixed item types) =====================
// v1 TEST DATASET: 2 of the ~14 planned progression areas (Kokiri Forest & Deku Tree; Hyrule
// Castle & Kakariko), enough to validate the whole mechanism end-to-end on hardware before
// authoring the rest. Adding more areas later is pure data (append a ChkCat) - the UI code below
// doesn't know or care how many there are.
//
// Detection "kind" (Auto-fill). Monotonic: only ever sets state 0->1, NEVER clears a mark - a
// consumed item can't silently un-tick real progress, and a manual un-check (state 2->3) is the
// only way an item goes back to unchecked; Auto-fill never touches state 1/2/3.
//   CK_MANUAL  - never auto-fillable (no reliable RAM signal known yet)
//   CK_EQUIP   - (R8(addr) & mask) != 0. A single confirmed bit. Two confirmed sources:
//                (a) equipment bits (swords/shields/tunics) - same byte/nibble the equip-toggle
//                cheats read/write; (b) the quest-status bitfield at 0x0587A14 (medallions bits
//                0-5, the 10 songs in bits 6-15/0x0587A15 + bits 16-17, spiritual stones bits
//                18-20/0x0587A16). The quest bit->item map was verified EMPIRICALLY by diffing 48
//                progressive OoT3D save files (HTW set): each newly-set bit lines up exactly with
//                the song/medallion/stone that save gained. Not a guess.
//   CK_QUESTALL- R8(addr) == mask. An exact "all N obtained" aggregate. Unused now that the
//                per-bit quest map is confirmed, but kept for the pattern.
//   CK_INVSLOT - the item lives in the game's fixed 24-slot inventory array at 0x5879E4 (proven
//                by the "All Items" cheat, which memcpys a 30-byte layout there). A slot holds the
//                item's ID, or 0xFF when empty. mask==0 -> present if R8(addr) != 0xFF (any item
//                in that slot); mask!=0 -> present only if R8(addr) == mask (an exact ID, for
//                slots shared by a child/adult pair, e.g. Hookshot 0x0A vs Longshot 0x0B).
//                Can only false-NEGATIVE (a consumable slot clearing), never false-positive, so
//                it's safe: a miss just leaves the item manual, exactly like today.
//   CK_UPGRADE - a multi-bit capacity field inside the "upgrade word" u32 at 0x0587A10. addr is
//                that u32; mask packs (shift<<3)|minLevel. got = ((R32(addr)>>shift)&0x7) >= min.
//                Field layout confirmed by save-diffing the HTW saves (each single-upgrade save
//                bumps exactly one field): quiver@0, bombBag@3, strength@6, scale@9, wallet@12,
//                bulletBag@14, stick@17, nut@20. minLevel lets "Silver Gauntlets" mean strength>=2.
enum { CK_MANUAL = 0, CK_EQUIP = 1, CK_QUESTALL = 2, CK_INVSLOT = 3, CK_UPGRADE = 4 };
enum { CKI_SPRITE, CKI_HEART, CKI_SKULL, CKI_NOTE, CKI_KEYITEM, CKI_NONE };

typedef struct {
    const char *key;    // stable save-key, never shown (so item text can be edited freely later)
    const char *task, *hint, *loc; // loc = "" -> no location to reveal
    u8 iconKind; u16 iconArg; u8 kind; // iconArg is u16: sprite keys 0x100+ (UI extras) exceed u8
    u32 addr; u8 mask;
} ChkItem;
typedef struct { const char *name; const ChkItem *items; int count; } ChkCat;
