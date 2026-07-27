# Generate sprites.h from the 42px-grid slices of the OoT3D item sheet.
# Format: RGBA4444 (u16), 16x16 for menu rows + 42x42 for picker preview.
from PIL import Image

# game item id (or 0x100+ pseudo-id for UI extras) -> grid cell (row, col)
MAP = {
    0x00: (3,0), 0x01: (3,1), 0x02: (3,2), 0x03: (3,3), 0x04: (3,4), 0x05: (3,5),
    0x06: (4,0), 0x07: (4,1), 0x08: (6,1), 0x09: (4,2), 0x0A: (4,3), 0x0B: (6,3),
    0x0C: (4,4), 0x0D: (4,5), 0x0E: (5,0), 0x0F: (5,1), 0x10: (5,2), 0x11: (5,3),
    0x12: (5,4), 0x13: (5,5),
    0x14: (8,0), 0x15: (8,1), 0x16: (8,2), 0x17: (8,3), 0x18: (8,4), 0x19: (8,5),
    0x1A: (9,0), 0x1B: (9,1), 0x1C: (9,2), 0x1D: (9,3), 0x1E: (9,4), 0x1F: (9,5),
    0x20: (10,0),
    0x21: (15,0), 0x22: (15,1), 0x23: (15,2),
    0x24: (12,0), 0x25: (12,1), 0x26: (12,2), 0x27: (12,3), 0x28: (12,4), 0x29: (12,5),
    0x2A: (13,0), 0x2B: (13,1), 0x2C: (6,5),
    0x2D: (15,3), 0x2E: (15,4), 0x2F: (15,5),
    0x30: (16,0), 0x31: (16,1), 0x32: (16,2), 0x33: (16,3), 0x34: (16,4), 0x35: (16,5),
    0x36: (17,0), 0x37: (17,1),
    0x38: (2,3), 0x39: (2,4), 0x3A: (2,5),
    0x3B: (19,0), 0x3C: (19,1), 0x3D: (19,2), 0x3E: (19,3), 0x3F: (19,4), 0x40: (19,5),
    0x41: (20,0), 0x42: (20,1), 0x43: (20,2), 0x45: (20,4),
    0x7C: (21,1),
    0xFF: (6,5),
    # UI extras (pseudo-ids)
    0x100: (33,2),  # single arrow (Infinite Arrows)
    0x101: (33,3),  # deku seeds (Slingshot ammo)
    0x102: (29,0),  # heart container
    0x103: (29,1),  # piece of heart
    0x104: (33,0),  # magic jar small
    0x105: (33,1),  # magic jar large
    0x106: (28,5),  # skulltula token
    0x107: (31,3),  # small key
    0x108: (31,2),  # dungeon map
    0x109: (31,1),  # compass
    0x10A: (31,0),  # boss key
    0x10B: (27,0),  # forest medallion (green)
    0x10C: (28,2),  # Zora's Sapphire (blue triple-orb)
    0x10D: (24,5),  # golden gauntlet
    0x10E: (24,4),  # silver gauntlet
    0x10F: (25,2),  # rupee bag
    0x110: (20,5),  # hover boots
    0x111: (28,0),  # gold swirl (spin attack)
    0x112: (24,3),  # gerudo token / crown
    0x113: (27,1),  # fire medallion (red)
    0x114: (27,2),  # water medallion (blue)
    0x115: (27,3),  # spirit medallion (orange)
    0x116: (27,4),  # shadow medallion (purple)
    0x117: (27,5),  # light medallion (yellow)
    0x118: (28,1),  # Goron's Ruby (red gem)
    0x119: (21,2),  # fishing rod (Fishing Pond checklist items)
}

def to4444(im):
    out = []
    for (r, g, b, a) in im.getdata():
        out.append(((r >> 4) << 12) | ((g >> 4) << 8) | ((b >> 4) << 4) | (a >> 4))
    return out

sheet = Image.open('oot3d_items.png').convert('RGBA')
keys = sorted(MAP.keys())

with open(r'..\rawplg\includes\sprites.h', 'w') as f:
    f.write("#pragma once\n")
    f.write("// Item icons from The Legend of Zelda: Ocarina of Time 3D.\n")
    f.write("// Sheet ripped by Colbydude (thanks Mystie) - The Spriters Resource.\n")
    f.write("// https://www.spriters-resource.com/3ds/thelegendofzeldaocarinaoftime3d/asset/62703/\n")
    f.write("// Converted to RGBA4444. Keys < 0x100 are game item IDs; 0x100+ are UI extras.\n\n")
    f.write("#define SPR16 16\n#define SPR42 42\n\n")

    for k in keys:
        r, c = MAP[k]
        cell = sheet.crop((c*42, r*42, (c+1)*42, (r+1)*42))
        i16 = cell.resize((16, 16), Image.LANCZOS)
        for name, im in ((f"spr16_{k:03X}", i16), (f"spr42_{k:03X}", cell)):
            data = to4444(im)
            f.write(f"static const unsigned short {name}[{len(data)}] = {{")
            f.write(','.join(str(v) for v in data))
            f.write("};\n")

    f.write("\ntypedef struct { unsigned short key; const unsigned short *px16; const unsigned short *px42; } SpriteRef;\n")
    f.write(f"#define NUM_SPRITES {len(keys)}\n")
    f.write("static const SpriteRef sprites[NUM_SPRITES] = {\n")
    for k in keys:
        f.write(f"    {{ 0x{k:03X}, spr16_{k:03X}, spr42_{k:03X} }},\n")
    f.write("};\n")

print('sprites.h written,', len(keys), 'sprites')
