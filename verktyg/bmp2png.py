#!/usr/bin/env python3
"""Gör om emulatorns BMP till PNG. Ren standardbibliotek, inget att installera.

sips klarar inte 32-bitars BMP med bitfältsmask, därför den här.
"""
import struct, sys, zlib


def bmp2png(in_fil, ut_fil):
    d = open(in_fil, "rb").read()
    offset = struct.unpack("<I", d[10:14])[0]
    bredd, hojd = struct.unpack("<ii", d[18:26])
    uppifran = hojd < 0
    hojd = abs(hojd)
    px = d[offset:offset + bredd * hojd * 4]

    rader = []
    for y in range(hojd):
        kalla = y if uppifran else hojd - 1 - y
        rad = bytearray(b"\x00")            # filtertyp 0
        start = kalla * bredd * 4
        for x in range(bredd):
            b, g, r, a = px[start + x * 4: start + x * 4 + 4]
            rad += bytes((r, g, b, a))      # BGRA -> RGBA
        rader.append(bytes(rad))

    radata = zlib.compress(b"".join(rader), 9)

    def bit(typ, data):
        return (struct.pack(">I", len(data)) + typ + data
                + struct.pack(">I", zlib.crc32(typ + data) & 0xFFFFFFFF))

    png = (b"\x89PNG\r\n\x1a\n"
           + bit(b"IHDR", struct.pack(">IIBBBBB", bredd, hojd, 8, 6, 0, 0, 0))
           + bit(b"IDAT", radata)
           + bit(b"IEND", b""))
    open(ut_fil, "wb").write(png)
    return bredd, hojd


if __name__ == "__main__":
    b, h = bmp2png(sys.argv[1], sys.argv[2])
    print(f"Skrev {sys.argv[2]} ({b}x{h})")
