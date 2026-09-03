#!/usr/bin/env python3
"""Renderar alla uttryck och lägger dem i ett rutnät, halv skala, med namn.

    python3 verktyg/kontaktark.py sim/bilder/kontaktark.png [--tid 900]

Eller ett rutnät av redan renderade BMP-filer, till exempel ett dygn:

    python3 verktyg/kontaktark.py sim/bilder/dygn.png --bmp "sim/bilder/dag-*.bmp" --kol 6

Ren standardbibliotek. Emulatorn måste vara byggd (sim/build/office-buddy-sim).
"""
import os, struct, subprocess, sys, tempfile, zlib

ROT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SIM = os.path.join(ROT, "sim", "build", "office-buddy-sim")
UTTRYCK = ["neutral", "glad", "väldigt glad", "förvånad", "entusiastisk", "nöjd",
           "blinkning", "ledsen", "besviken", "orolig", "arg", "fundersam",
           "trött", "sömnig", "gäspar", "stressad", "nyfiken", "kär", "överväldigad", "sover"]

def las_bmp(fil):
    d = open(fil, "rb").read()
    offset = struct.unpack("<I", d[10:14])[0]
    b, h = struct.unpack("<ii", d[18:26])
    h = abs(h)
    px = d[offset:offset + b * h * 4]
    return b, h, px

def halv(b, h, px):
    """Halverar med medelvärde av 2x2, ger mjukare kanter än att hoppa över."""
    ub, uh = b // 2, h // 2
    ut = bytearray(ub * uh * 3)
    for y in range(uh):
        r0 = (2 * y) * b * 4; r1 = r0 + b * 4
        for x in range(ub):
            i0 = r0 + 8 * x; i1 = r1 + 8 * x
            o = (y * ub + x) * 3
            for k, kk in ((0, 2), (1, 1), (2, 0)):   # BGRA -> RGB
                ut[o + k] = (px[i0 + kk] + px[i0 + 4 + kk] + px[i1 + kk] + px[i1 + 4 + kk]) >> 2
    return ub, uh, ut

def skriv_png(fil, b, h, rgb):
    rader = b"".join(b"\x00" + bytes(rgb[y * b * 3:(y + 1) * b * 3]) for y in range(h))
    def bit(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    open(fil, "wb").write(b"\x89PNG\r\n\x1a\n" + bit(b"IHDR", struct.pack(">IIBBBBB", b, h, 8, 2, 0, 0, 0))
                          + bit(b"IDAT", zlib.compress(rader, 9)) + bit(b"IEND", b""))

def main():
    ut = sys.argv[1]
    tid = "900"
    extra = []
    kol, marg = 5, 6
    monster = None
    a = sys.argv[2:]
    while a:
        if a[0] == "--tid": tid = a[1]; a = a[2:]
        elif a[0] == "--bmp": monster = a[1]; a = a[2:]
        elif a[0] == "--kol": kol = int(a[1]); a = a[2:]
        else: extra.append(a.pop(0))
    rutor = []
    if monster:
        import glob
        for fil in sorted(glob.glob(monster)):
            rutor.append(halv(*las_bmp(fil)))
    else:
      with tempfile.TemporaryDirectory() as tmp:
        for namn in UTTRYCK:
            bmp = os.path.join(tmp, "u.bmp")
            subprocess.run([SIM, "--bild", bmp, "--uttryck", namn, "--tid", tid] + extra,
                           check=True, stdout=subprocess.DEVNULL)
            rutor.append(halv(*las_bmp(bmp)))
    rb, rh = rutor[0][0], rutor[0][1]
    rader = (len(rutor) + kol - 1) // kol
    B, H = kol * (rb + marg) + marg, rader * (rh + marg) + marg
    duk = bytearray(B * H * 3)
    for y in range(H):
        for x in range(B):
            duk[(y * B + x) * 3:(y * B + x) * 3 + 3] = b"\x22\x22\x22"
    for i, (b, h, px) in enumerate(rutor):
        ox = marg + (i % kol) * (rb + marg); oy = marg + (i // kol) * (rh + marg)
        for y in range(h):
            duk[((oy + y) * B + ox) * 3:((oy + y) * B + ox + b) * 3] = px[y * b * 3:(y + 1) * b * 3]
    skriv_png(ut, B, H, duk)
    print("Skrev", ut, f"({B}x{H})", "" if monster else "ordning: " + ", ".join(UTTRYCK))

if __name__ == "__main__":
    main()
