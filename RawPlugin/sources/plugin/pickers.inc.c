// ===================== Pickers (bottle contents / inventory item) =====================
typedef struct { const char *name; u8 val; } PickOpt;
typedef struct { const char *title; const PickOpt *opts; int count; u32 addr; } Picker;

static const PickOpt bottleOpts[] = {
    { "Red Potion", 0x15 }, { "Green Potion", 0x16 }, { "Blue Potion", 0x17 },
    { "Fairy", 0x18 }, { "Fish", 0x19 }, { "Milk (2 doses)", 0x1A }, { "Milk (1 dose)", 0x1F },
    { "Letter", 0x1B }, { "Blue Flame", 0x1C }, { "Insect", 0x1D }, { "Soul", 0x1E },
    { "Spirit", 0x20 }, { "Empty Bottle", 0x14 }, { "Locked (careful!)", 0xFF },
};

static const PickOpt invOpts[] = {
    { "Deku Stick", 0x00 }, { "Deku Nut", 0x01 }, { "Bomb", 0x02 }, { "Fairy Bow", 0x03 },
    { "Fire Arrow", 0x04 }, { "Din's Fire", 0x05 }, { "Fairy Slingshot", 0x06 },
    { "Fairy Ocarina", 0x07 }, { "Ocarina of Time", 0x08 }, { "Bombchu", 0x09 },
    { "Hookshot", 0x0A }, { "Longshot", 0x0B }, { "Ice Arrow", 0x0C }, { "Farore's Wind", 0x0D },
    { "Boomerang", 0x0E }, { "Lens of Truth", 0x0F }, { "Magic Beans", 0x10 },
    { "Megaton Hammer", 0x11 }, { "Light Arrow", 0x12 }, { "Nayru's Love", 0x13 },
    { "Weird Egg", 0x21 }, { "Cucco", 0x22 }, { "Zelda's Letter", 0x23 },
    { "Keaton Mask", 0x24 }, { "Skull Mask", 0x25 }, { "Spooky Mask", 0x26 },
    { "Bunny Hood", 0x27 }, { "Goron Mask", 0x28 }, { "Zora Mask", 0x29 },
    { "Gerudo Mask", 0x2A }, { "Mask of Truth", 0x2B }, { "No Mask", 0x2C },
    { "Pocket Egg", 0x2D }, { "Pocket Cucco", 0x2E }, { "Cojiro", 0x2F },
    { "Odd Mushroom", 0x30 }, { "Odd Poultice", 0x31 }, { "Poacher's Saw", 0x32 },
    { "Broken Goron Sword", 0x33 }, { "Prescription", 0x34 }, { "Eyeball Frog", 0x35 },
    { "World Finest Eyedrops", 0x36 }, { "Claim Check", 0x37 },
    { "Fairy Bow (Fire)", 0x38 }, { "Fairy Bow (Ice)", 0x39 }, { "Fairy Bow (Light)", 0x3A },
    { "Kokiri Sword", 0x3B }, { "Master Sword", 0x3C }, { "Giant's Knife", 0x3D },
    { "Deku Shield", 0x3E }, { "Hylian Shield", 0x3F }, { "Mirror Shield", 0x40 },
    { "Kokiri Tunic", 0x41 }, { "Goron Tunic", 0x42 }, { "Zora Tunic", 0x43 },
    { "Iron Boots", 0x45 }, { "Biggoron's Sword", 0x7C }, { "Empty", 0xFF },
};

enum { PK_BOTTLE1, PK_BOTTLE2, PK_BOTTLE3, PK_INV, NUM_PICKERS };
static const Picker pickers[NUM_PICKERS] = {
    { "Bottle #1",          bottleOpts, 14, 0x05879F6 },
    { "Bottle #2",          bottleOpts, 14, 0x05879F7 },
    { "Bottle #3",          bottleOpts, 14, 0x05879F8 },
    { "Inventory Modifier", invOpts,    58, 0x05879F8 },
};

// Mapped teleport destinations. Entrance indices are OoT3D values (from gamestabled's entrances.c,
// NOT the N64 wrong-warp tables). age -1 = keep Link's current age. See [[oot3d-warp-teleport-map]].
typedef struct { const char *name; u16 entrance; s8 age; const char *desc; } Warp;
static const Warp warps[] = {
    /* 0 */  { "Reload current scene",   0xFFFF, -1, "Reloads the area you're in. The safest warp - use it first to confirm teleport works on your game." },
    // --- Overworld & towns (indices 1..18) ---
    /* 1 */  { "Kokiri Forest",          0x00EE, -1, "Warp to Kokiri Forest." },
    /* 2 */  { "Lost Woods",             0x011E, -1, "Warp to the Lost Woods." },
    /* 3 */  { "Link's House",           0x0272, -1, "Warp inside Link's house." },
    /* 4 */  { "Hyrule Field",           0x00CD, -1, "Warp to Hyrule Field." },
    /* 5 */  { "Market",                 0x00B1, -1, "Warp to the Castle Town Market." },
    /* 6 */  { "Temple of Time",         0x0053, -1, "Warp to the Temple of Time." },
    /* 7 */  { "Lon Lon Ranch",          0x0157, -1, "Warp to Lon Lon Ranch." },
    /* 8 */  { "Kakariko Village",       0x00DB, -1, "Warp to Kakariko Village." },
    /* 9 */  { "Death Mtn Trail",       0x013D, -1, "Warp to the Death Mountain Trail." },
    /* 10 */ { "Death Mtn Crater",      0x0147, -1, "Warp to Death Mountain Crater." },
    /* 11 */ { "Goron City",             0x014D, -1, "Warp to Goron City." },
    /* 12 */ { "Zora's River",           0x00EA, -1, "Warp to Zora's River." },
    /* 13 */ { "Zora's Domain",          0x0108, -1, "Warp to Zora's Domain." },
    /* 14 */ { "Zora's Fountain",        0x010E, -1, "Warp to Zora's Fountain." },
    /* 15 */ { "Lake Hylia",             0x0102, -1, "Warp to Lake Hylia." },
    /* 16 */ { "Gerudo Valley",          0x0117, -1, "Warp to Gerudo Valley." },
    /* 17 */ { "Wasteland",              0x0130, -1, "Warp to the Haunted Wasteland." },
    /* 18 */ { "Desert Colossus",        0x0123, -1, "Warp to the Desert Colossus." },
    // --- Dungeons (indices 19..29) ---
    /* 19 */ { "Deku Tree",              0x0000, -1, "Warp inside the Great Deku Tree." },
    /* 20 */ { "Dodongo's Cavern",       0x0004, -1, "Warp inside Dodongo's Cavern." },
    /* 21 */ { "Jabu-Jabu",             0x0028, -1, "Warp inside Jabu-Jabu's Belly." },
    /* 22 */ { "Forest Temple",          0x0169, -1, "Warp inside the Forest Temple." },
    /* 23 */ { "Fire Temple",            0x0165, -1, "Warp inside the Fire Temple." },
    /* 24 */ { "Water Temple",           0x0010, -1, "Warp inside the Water Temple." },
    /* 25 */ { "Shadow Temple",          0x0037, -1, "Warp inside the Shadow Temple." },
    /* 26 */ { "Spirit Temple",          0x0082, -1, "Warp inside the Spirit Temple." },
    /* 27 */ { "Ice Cavern",             0x0088, -1, "Warp inside the Ice Cavern." },
    /* 28 */ { "Gerudo Training",        0x0008, -1, "Warp inside the Gerudo Training Ground." },
    /* 29 */ { "Ganon's Castle",         0x0467, -1, "Warp inside Ganon's Castle." },
};
#define NUM_WARPS (int)(sizeof(warps)/sizeof(warps[0]))
static u8 warpFav[NUM_WARPS]; // teleport destinations starred for the quick menu (own Favorites lines, '@'-prefixed)

