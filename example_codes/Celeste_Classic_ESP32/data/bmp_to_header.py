from PIL import Image

def convert(path, name, w, h, outfile):
    img = Image.open(path)
    px = list(img.getdata())
    assert len(px) == w * h, f"Expected {w*h} pixels, got {len(px)}"
    with open(outfile, "w") as f:
        f.write("#pragma once\n")
        f.write("#include <pgmspace.h>\n")
        f.write(f"static const uint8_t {name}[{w*h}] PROGMEM = {{\n")
        for i in range(0, len(px), 16):
            f.write("  " + ", ".join(f"0x{v:02x}" for v in px[i:i+16]) + ",\n")
        f.write("};\n")
    print(f"Written {outfile}")

convert("gfx.bmp",  "gfxPixels_P",  128, 64,  "gfx_data.h")
convert("font.bmp", "fontPixels_P", 128, 85, "font_data.h")