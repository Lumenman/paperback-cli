"""Warp a page bitmap the way a scanner does: local step drifts across the sheet."""
import struct, subprocess, sys
from pathlib import Path

def load(path):
    b = bytearray(Path(path).read_bytes())
    w, h = struct.unpack_from('<ii', b, 18)
    off = struct.unpack_from('<I', b, 10)[0]
    return b, w, h, off, (w + 3) & ~3

def warp(src, dst, d):
    """srcy = y + d*h*((y/h)^2 - y/h): ends fixed, local step runs 1-d .. 1+d."""
    b, w, h, off, stride = load(src)
    px = bytes(b[off:off + stride * h])
    out = bytearray(px)
    for y in range(h):
        t = y / h
        sy = y + d * h * (t * t - t)
        i = int(sy)
        f = sy - i
        i = min(max(i, 0), h - 2)
        a = px[i * stride:i * stride + w]
        c = px[(i + 1) * stride:(i + 1) * stride + w]
        out[y * stride:y * stride + w] = bytes(
            int(a[x] + (c[x] - a[x]) * f) for x in range(w))
    b[off:off + stride * h] = out
    Path(dst).write_bytes(b)

if __name__ == '__main__':
    warp(sys.argv[1], sys.argv[2], float(sys.argv[3]))
