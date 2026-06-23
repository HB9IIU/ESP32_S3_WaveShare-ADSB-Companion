#!/usr/bin/env python3
"""
Convert TT Bluescreens Trial Regular.ttf subset (digits + colon) to LovyanGFX VLW format.

LovyanGFX VLW binary layout (all int32 big-endian):
  Header (24 bytes):
    [0] gCount   – number of glyphs
    [1] version  – 1
    [2] font_size – ascent + descent
    [3] 0        – reserved
    [4] ascent   – pixels above baseline
    [5] descent  – pixels below baseline (positive)

  Per-glyph entry (28 bytes, starting at offset 24):
    [0] unicode   – codepoint
    [1] height    – glyph bitmap height
    [2] width     – glyph bitmap width
    [3] xAdvance  – advance cursor by this many pixels
    [4] dY        – pixels from baseline to TOP of bitmap (positive = above)
    [5] dX        – pixels from cursor-x to left of bitmap (can be negative)
    [6] 0         – padding

  Bitmap data (8bpp grayscale, row-major):
    width * height bytes per glyph, same order as headers
"""

import struct
import sys
from pathlib import Path
import freetype

TTF_PATH   = Path("fonts/tt_bluescreens/TT Bluescreens Trial Regular.ttf")
OUTPUT_DIR = Path("data")
CHARS      = "0123456789:"

# Screen: 800×480, split into two 400 px halves.
# "HH:MM" must fit within MAX_WIDTH pixels centred in each half.
MAX_WIDTH = 375

def render_glyphs(face, chars):
    glyphs = []
    for ch in chars:
        face.load_char(ch, freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_NORMAL)
        slot   = face.glyph
        bm     = slot.bitmap
        w, h   = bm.width, bm.rows
        glyphs.append(dict(
            unicode  = ord(ch),
            width    = w,
            height   = h,
            xAdvance = slot.advance.x >> 6,
            dY       = slot.bitmap_top,           # above baseline → positive
            dX       = slot.bitmap_left,           # may be negative
            bitmap   = bytes(bm.buffer[:w * h]),
        ))
    return glyphs

def hmm_width(glyphs):
    """Return total pixel width of 'HH:MM' (4 digits + 1 colon)."""
    by_code = {g['unicode']: g for g in glyphs}
    # Use '0' as representative digit (often the widest)
    d = by_code[ord('0')]['xAdvance']
    c = by_code[ord(':')]['xAdvance']
    return 4 * d + c

def best_size(ttf_path, max_width, hi=200, lo=80):
    face = freetype.Face(str(ttf_path))
    for sz in range(hi, lo - 1, -1):
        face.set_pixel_sizes(0, sz)
        glyphs = render_glyphs(face, CHARS)
        if hmm_width(glyphs) <= max_width:
            return sz, glyphs
    return lo, render_glyphs(face, CHARS)

def build_vlw(glyphs, out_path):
    ascent  = max(g['dY']              for g in glyphs)
    descent = max(g['height'] - g['dY'] for g in glyphs)
    fs      = ascent + descent
    count   = len(glyphs)

    data = bytearray()
    # Header
    for v in (count, 1, fs, 0, ascent, descent):
        data += struct.pack('>i', v)

    # Glyph headers
    for g in glyphs:
        for v in (g['unicode'], g['height'], g['width'],
                  g['xAdvance'], g['dY'], g['dX'], 0):
            data += struct.pack('>i', v)

    # Bitmaps
    for g in glyphs:
        data += g['bitmap']

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(data)
    return fs, ascent, descent, data

def main():
    target_size = int(sys.argv[1]) if len(sys.argv) > 1 else 0

    if target_size:
        face = freetype.Face(str(TTF_PATH))
        face.set_pixel_sizes(0, target_size)
        glyphs = render_glyphs(face, CHARS)
        sz = target_size
    else:
        print(f"Auto-selecting size to fit HH:MM ≤ {MAX_WIDTH} px …")
        sz, glyphs = best_size(TTF_PATH, MAX_WIDTH)

    out_path = OUTPUT_DIR / f"bluescreens{sz}.vlw"
    fs, ascent, descent, data = build_vlw(glyphs, out_path)

    print(f"\nFont size requested: {sz} px")
    print(f"  ascent={ascent}  descent={descent}  total={fs} px")
    print(f"  HH:MM pixel width = {hmm_width(glyphs)} px  (max {MAX_WIDTH})")
    print()
    for g in glyphs:
        print(f"  '{chr(g['unicode'])}': w={g['width']:3d} h={g['height']:3d}  "
              f"xAdv={g['xAdvance']:3d}  dX={g['dX']:3d}  dY={g['dY']:3d}")
    print()
    print(f"Written {len(data):,} bytes → {out_path}")
    print()
    print("In clock_screensaver.h use:")
    print(f'  constexpr const char* kFontPath = "/{out_path.name}";')
    print(f"  constexpr int32_t kFontAscent   = {ascent};")
    print(f"  constexpr int32_t kFontHeight   = {fs};")

if __name__ == "__main__":
    main()
