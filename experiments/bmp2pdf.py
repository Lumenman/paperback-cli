"""Turn encoder BMP pages into one PDF of 1-bit stencils (/ImageMask), so the printer places the
dots at its own resolution: a stencil has no grey edges to smooth, whatever the scale.

Dots (grey < 100) go into one stencil, the grey header text (100..199) into a second one painted
grey, as the BMP has it. The encoder draws dots grey 64, not black; --dot-gray 0.25 keeps that,
the default 0 prints them solid black, so a comparison against the BMP changes one thing at a time
only with 0.25.

usage: python experiments/bmp2pdf.py -o sheet.pdf sheet_0001.bmp sheet_0002.bmp ...
       python experiments/bmp2pdf.py --check      # self-check, no files needed
"""
import argparse, struct, sys, zlib
from pathlib import Path


def load(path):
    """-> (width, height, dpi_x, dpi_y, rows of grey bytes top to bottom)."""
    b = Path(path).read_bytes()
    off, = struct.unpack_from('<I', b, 10)
    hsize, w, h, _, bpp, comp = struct.unpack_from('<IiiHHI', b, 14)
    ppmx, ppmy = struct.unpack_from('<ii', b, 38)
    if b[:2] != b'BM' or comp != 0 or bpp not in (8, 24):
        sys.exit(f'{path}: only uncompressed 8-bit paletted or 24-bit BMP')
    stride = (w * bpp // 8 + 3) & ~3
    if bpp == 8:  # palette BGRA -> grey, as a translate table
        pal = b[14 + hsize:off]
        lut = bytes((pal[4 * i] + pal[4 * i + 1] + pal[4 * i + 2]) // 3 if 4 * i + 3 < len(pal) else 255
                    for i in range(256))
        rows = [b[off + y * stride:off + y * stride + w].translate(lut) for y in range(abs(h))]
    else:
        rows = [bytes(sum(r[3 * x:3 * x + 3]) // 3 for x in range(w))
                for r in (b[off + y * stride:off + y * stride + 3 * w] for y in range(abs(h)))]
    if h > 0:
        rows.reverse()  # bottom-up
    dpi = lambda ppm: round(ppm * 0.0254) or None
    return w, abs(h), dpi(ppmx), dpi(ppmy), rows


def mask(rows, lo, hi):
    """1-bit stencil rows, bit 0 = paint (the ImageMask default), for lo <= grey < hi."""
    lut = bytes(0x30 if lo <= g < hi else 0x31 for g in range(256))  # '0' / '1'
    w = len(rows[0])
    pad = (-w) % 8
    return b''.join(int(r.translate(lut) + b'1' * pad, 2).to_bytes((w + pad) // 8, 'big') for r in rows)


def pdf(pages):
    """pages: [(w, h, dpi_x, dpi_y, rows, dot_gray)] -> PDF bytes."""
    objs, kids = ['<< /Type /Catalog /Pages 2 0 R >>', None], []
    for w, h, dx, dy, rows, dot_gray in pages:
        pw, ph = w * 72 / dx, h * 72 / dy
        names, ops = [], []
        for name, lo, hi, gray in (('D', 0, 100, dot_gray), ('T', 100, 200, 128 / 255)):
            data = zlib.compress(mask(rows, lo, hi), 9)
            objs.append(b'<< /Type /XObject /Subtype /Image /Width %d /Height %d /ImageMask true '
                        b'/Interpolate false /Filter /FlateDecode /Length %d >>\nstream\n' % (w, h, len(data))
                        + data + b'\nendstream')
            names.append(f'/{name} {len(objs)} 0 R')
            ops.append(f'q {gray:.4f} g {pw:.4f} 0 0 {ph:.4f} 0 0 cm /{name} Do Q')
        content = '\n'.join(ops).encode()
        objs.append(b'<< /Length %d >>\nstream\n' % len(content) + content + b'\nendstream')
        objs.append(f'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 {pw:.4f} {ph:.4f}] '
                    f'/Resources << /XObject << {" ".join(names)} >> >> /Contents {len(objs)} 0 R >>')
        kids.append(f'{len(objs)} 0 R')
    objs[1] = f'<< /Type /Pages /Kids [{" ".join(kids)}] /Count {len(kids)} >>'
    out, offs = bytearray(b'%PDF-1.4\n%\xe2\xe3\xcf\xd3\n'), []
    for k, o in enumerate(objs, 1):
        offs.append(len(out))
        out += b'%d 0 obj\n' % k + (o if isinstance(o, bytes) else o.encode()) + b'\nendobj\n'
    xref = len(out)
    out += b'xref\n0 %d\n0000000000 65535 f \n' % (len(objs) + 1)
    out += b''.join(b'%010d 00000 n \n' % o for o in offs)
    out += b'trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n' % (len(objs) + 1, xref)
    return bytes(out)


def check():
    rows = [bytes([64, 255, 128, 255, 64, 64, 255, 255, 64]), bytes([255, 64, 255, 128, 255, 255, 64, 64, 255])]
    assert mask(rows, 0, 100) == bytes([0b01110011, 0b01111111, 0b10111100, 0b11111111])
    assert mask(rows, 100, 200) == bytes([0b11011111, 0b11111111, 0b11101111, 0b11111111])
    doc = pdf([(9, 2, 600, 300, rows, 0.0)])
    assert doc.count(b'/ImageMask true') == 2 and b'/Count 1' in doc
    assert b'/MediaBox [0 0 1.0800 0.4800]' in doc  # 9 px / 600 dpi, 2 px / 300 dpi, in points
    start = doc.index(b'stream\n') + 7
    assert zlib.decompress(doc[start:doc.index(b'\nendstream', start)]) == mask(rows, 0, 100)
    print('bmp2pdf: check passed')


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('bmp', nargs='*')
    ap.add_argument('-o', '--output')
    ap.add_argument('--dpi', type=int, help='resolution when the BMP records none')
    ap.add_argument('--dot-gray', type=float, default=0.0, help='0 black (default), 0.25 = the BMP grey 64')
    ap.add_argument('--check', action='store_true')
    a = ap.parse_args()
    if a.check:
        check(); sys.exit()
    if not a.bmp or not a.output:
        ap.error('need BMP pages and -o')
    pages = []
    for p in a.bmp:
        w, h, dx, dy, rows = load(p)
        dx, dy = dx or a.dpi, dy or a.dpi
        if not dx or not dy:
            sys.exit(f'{p}: no resolution recorded, pass --dpi')
        pages.append((w, h, dx, dy, rows, a.dot_gray))
    Path(a.output).write_bytes(pdf(pages))
    print(f'{a.output}: {len(pages)} page(s), {w / dx * 25.4:.1f} x {h / dy * 25.4:.1f} mm, '
          f'{dx}x{dy} dpi stencil, dots grey {a.dot_gray}')
