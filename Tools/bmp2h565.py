import struct, sys

def convert(path, name, out):
    d = open(path, 'rb').read()
    off = struct.unpack_from('<I', d, 10)[0]
    w = struct.unpack_from('<i', d, 18)[0]
    h = struct.unpack_from('<i', d, 22)[0]
    bpp = struct.unpack_from('<H', d, 28)[0]
    assert bpp == 24, f"bpp={bpp}"
    stride = (w * 3 + 3) & ~3
    H = abs(h)
    out.write(f"#define {name.upper()}_W {w}\n#define {name.upper()}_H {H}\n")
    out.write(f"static const unsigned short {name}[{w*H}] = {{\n")
    vals = []
    for y in range(H):
        srcy = (H - 1 - y) if h > 0 else y
        row = off + srcy * stride
        for x in range(w):
            b, g, r = d[row+x*3], d[row+x*3+1], d[row+x*3+2]
            vals.append(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
    for i in range(0, len(vals), 16):
        out.write(','.join(str(v) for v in vals[i:i+16]) + ',\n')
    out.write("};\n")

with open(sys.argv[3], 'w') as out:
    out.write("#pragma once\n// RGB565, generated from original .plg background BMPs\n")
    convert(sys.argv[1], sys.argv[2], out)
print("ok")
