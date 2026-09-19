"""Dot-level diff: what the paper kept, measured against the exact source image.

A decode answers "did Reed-Solomon save it". This answers the question under
it: of the dots that were sent to the printer, how many come back readable at
all. The sheet and its scan are the same nominal size, so the raster bounding
box of each gives a first scale and offset, nine patches refine it into an
affine, and then every cell of the source lattice is sampled in the scan and
compared with the ink that was printed there. No grid search, no ECC, so a
failure here is paper and optics, not the decoder.

  python experiments/dot_diff.py SOURCE.bmp SCAN.bmp CELLPX
  python experiments/dot_diff.py --self-test

The ink/paper grey separation is the honesty check: if the fit is wrong the two
are equal, because then every sample is a coin toss.
"""
import sys
import numpy as np
from PIL import Image

Image.MAX_IMAGE_PIXELS = None


def load(path):
    return np.asarray(Image.open(path).convert('L'), dtype=np.float32)


def widestrun(count, smooth=1):
    """Start and end of the widest run of rows carrying real raster ink.

    Not the first and last dark row: a printed header, a footer or a mark on the
    glass is dark too, and none of them is anywhere near as dense as the raster.
    Same idea as the decoder's own raster location. The profile is smoothed
    first, because a raster of dots is itself a row of gaps."""
    if smooth > 1:
        count = np.convolve(count, np.ones(smooth) / smooth, 'same')
    on = count > count.max() / 4.0
    best = (0, -1)
    i = 0
    while i < len(on):
        if on[i]:
            j = i
            while j + 1 < len(on) and on[j + 1]:
                j += 1
            if j - i > best[1] - best[0]:
                best = (i, j)
            i = j
        i += 1
    return best


def inkbox(img):
    """Bounding box of the raster, ignoring header, footer and dirt."""
    dark = img < (int(img.min()) + int(np.percentile(img, 99))) // 2
    x0, x1 = widestrun(dark.sum(0).astype(float), 32)
    y0, y1 = widestrun(dark.sum(1).astype(float), 32)
    return x0, y0, x1, y1


def locate(src, scan, cx, cy, sx, sy, half=192, win=384):
    """Where in the scan the source patch centred at (cx,cy) sits, searching a
    window around the predicted (sx,sy). FFT correlation, so the window costs
    nothing; it is clamped to the image, which only shifts the search."""
    sx = int(np.clip(sx, win, scan.shape[1] - win))
    sy = int(np.clip(sy, win, scan.shape[0] - win))
    a = src[cy - half:cy + half, cx - half:cx + half]
    b = scan[sy - win:sy + win, sx - win:sx + win]
    pad = np.zeros_like(b)
    pad[:2 * half, :2 * half] = a - a.mean()
    c = np.fft.irfft2(np.fft.rfft2(b - b.mean()) * np.conj(np.fft.rfft2(pad)), b.shape)
    dy, dx = np.unravel_index(np.argmax(c), c.shape)
    return sx - win + dx + half, sy - win + dy + half


def square(M):
    return np.vstack([M, [0.0, 0.0, 1.0]])


def warp(img, M):
    """The image in the other one's geometry. PIL wants the destination-to-source
    map, so it gets the inverse."""
    inv = np.linalg.inv(square(M))[:2].ravel()
    return np.asarray(Image.fromarray(img).transform(
        (img.shape[1], img.shape[0]), Image.AFFINE, tuple(inv), Image.BICUBIC,
        fillcolor=255), dtype=np.float32)


def fit(src, scan, box=None, sbox=None, inset=300):
    """Affine source->scan, [[a,b,tx],[c,d,ty]], and the worst patch residual.

    The bounding boxes give the printer's scaling first. That step is not
    cosmetic: a few percent of scale is more than a cell of drift across a
    patch, and correlating a patch against a differently scaled copy of itself
    finds a spurious peak. So the source is first warped by the box estimate,
    and the patches then only measure what is left. Patches sit inside the
    raster - blank paper correlates with anything, and one bad patch tilts the
    whole fit."""
    x0, y0, x1, y1 = box if box is not None else inkbox(src)
    u0, v0, u1, v1 = sbox if sbox is not None else inkbox(scan)
    kx, ky = (u1 - u0) / (x1 - x0), (v1 - v0) / (y1 - y0)
    M0 = np.array([[kx, 0.0, u0 - kx * x0], [0.0, ky, v0 - ky * y0]])
    coarse = warp(src, M0)
    pts, obs = [], []
    for cy in np.linspace(y0 + inset, y1 - inset, 3):
        for cx in np.linspace(x0 + inset, x1 - inset, 3):
            px, py = M0[0, 0] * cx + M0[0, 2], M0[1, 1] * cy + M0[1, 2]
            px, py = int(px), int(py)
            pts.append((px, py, 1.0))
            obs.append(locate(coarse, scan, px, py, px, py))
    A, B = np.array(pts, dtype=np.float64), np.array(obs, dtype=np.float64)
    M1 = np.linalg.lstsq(A, B, rcond=None)[0].T
    return (square(M1) @ square(M0))[:2], np.abs(A @ M1.T - B).max()


def dotspan(ink, cell):
    """Which pixels of a cell the dot covers, measured on the raster itself.

    A dot does not sit in the middle of its cell: the encoder puts it in one
    corner and the rest is the gap. Where exactly follows from -s and from
    rounding to whole pixels, so measuring the ink is cheaper than reproducing
    the encoder's arithmetic - and sampling the gap reads paper for every dot."""
    perphase = np.array([ink[i::cell].mean() for i in range(cell)])
    lo, hi = widestrun(perphase - perphase.min())
    return lo, hi - lo + 1


def lattice(src, cell, box=None):
    """Dot corners of the source raster, and the dot size in pixels."""
    x0, y0, x1, y1 = box if box is not None else inkbox(src)
    cell = int(cell)
    dark = (src[y0:y1 + 1, x0:x1 + 1] < 128).astype(float)
    (ox, wx), (oy, wy) = dotspan(dark.mean(0), cell), dotspan(dark.mean(1), cell)
    return (np.arange(x0 + ox, x1 - wx, cell), np.arange(y0 + oy, y1 - wy, cell),
            wx, wy)


def samplexy(img, X, Y, w, h):
    """Mean grey over the dot area at each given position."""
    xi, yi = np.round(X).astype(int), np.round(Y).astype(int)
    np.clip(xi, 0, img.shape[1] - w - 1, out=xi)
    np.clip(yi, 0, img.shape[0] - h - 1, out=yi)
    return sum(img[yi + dy, xi + dx] for dy in range(h) for dx in range(w)) / (w * h)


def localshifts(src, aligned, gx, gy, cell, tile):
    """One residual shift per tile of `tile` cells. A single affine cannot follow
    paper: the sheet stretches unevenly through the printer and again on the
    glass, and half a cell of leftover drift reads the neighbouring dot. The
    decoder answers this by locating every block; here a tile does the same job
    over a block-sized piece of the page."""
    half = int(tile * cell / 2)
    ny, nx = -(-len(gy) // tile), -(-len(gx) // tile)
    SX, SY = np.zeros((ny, nx)), np.zeros((ny, nx))
    for j in range(ny):
        for i in range(nx):
            cx = int(gx[min(i * tile + tile // 2, len(gx) - 1)])
            cy = int(gy[min(j * tile + tile // 2, len(gy) - 1)])
            cx = int(np.clip(cx, half, src.shape[1] - half - 1))
            cy = int(np.clip(cy, half, src.shape[0] - half - 1))
            fx, fy = locate(src, aligned, cx, cy, cx, cy, half=half, win=2 * half)
            SX[j, i], SY[j, i] = fx - cx, fy - cy
    return (np.repeat(np.repeat(SX, tile, 0), tile, 1)[:len(gy), :len(gx)],
            np.repeat(np.repeat(SY, tile, 0), tile, 1)[:len(gy), :len(gx)])


def report(source, scanfile, cell, tile=32):
    src, scan = load(source), load(scanfile)
    box, sbox = inkbox(src), inkbox(scan)
    M, resid = fit(src, scan, box, sbox)
    aligned = warp(scan, np.linalg.inv(square(M))[:2])   # the scan in sheet geometry
    gx, gy, wx, wy = lattice(src, cell, box)
    X, Y = np.meshgrid(gx, gy)
    SX, SY = localshifts(src, aligned, gx, gy, cell, tile)
    ink = samplexy(src, X, Y, wx, wy) < 128          # what was printed
    got = samplexy(aligned, X + SX, Y + SY, wx, wy)  # what came back
    print(f"{source} -> {scanfile}")
    print(f"  raster {box[2]-box[0]+1}x{box[3]-box[1]+1} px on the sheet, "
          f"{sbox[2]-sbox[0]+1}x{sbox[3]-sbox[1]+1} px on the scan")
    print(f"  tile shift {SX.min():+.0f}..{SX.max():+.0f} x, "
          f"{SY.min():+.0f}..{SY.max():+.0f} y px over {tile}-cell tiles")
    print(f"  {len(gx)}x{len(gy)} cells, dot {wx}x{wy} px, {ink.sum()} inked; affine scale "
          f"{M[0,0]:.4f},{M[1,1]:.4f} skew {M[0,1]:+.4f},{M[1,0]:+.4f} "
          f"offset {M[0,2]:+.0f},{M[1,2]:+.0f} px, residual {resid:.1f} px")
    lo, hi = got[ink].mean(), got[~ink].mean()
    print(f"  grey: ink {lo:.0f}, paper {hi:.0f}, separation {hi-lo:.0f}")
    err, t = min((np.count_nonzero((got < t) != ink), t) for t in range(20, 250, 2))
    print(f"  best threshold {t}: {err} of {ink.size} cells wrong = {100.0*err/ink.size:.3f}%"
          f"  (missed dots {np.count_nonzero(~(got<t) & ink)}, "
          f"false dots {np.count_nonzero((got<t) & ~ink)})")
    mid = (lo + hi) / 2.0
    print(f"  midpoint threshold {mid:.0f}: "
          f"{100.0*np.count_nonzero((got<mid)!=ink)/ink.size:.3f}% wrong")
    wrong = ((got < t) != ink)
    qh, qw = len(gy) // 2, len(gx) // 2
    print("  by quadrant %: " + " ".join(f"{100.0*wrong[a:a+qh, b:b+qw].mean():.3f}"
                                        for a in (0, qh) for b in (0, qw)))
    return err, ink.size


def self_test():
    """A known affine on synthetic dots must come back, and read back clean."""
    rng = np.random.default_rng(7)
    src = np.full((2400, 2400), 255.0, np.float32)
    dots = rng.random((299, 299)) < 0.5              # 4px dots on an 8px lattice
    unit = np.zeros((8, 8), bool); unit[:4, :4] = True
    ink = np.kron(dots, unit)
    src[2:2 + ink.shape[0], 2:2 + ink.shape[1]][ink] = 0.0
    M = np.array([[0.972, 0.001, 13.0], [-0.001, 0.973, -9.0]])
    inv = np.linalg.inv(np.vstack([M, [0, 0, 1]]))   # scan(x) = src(inv x)
    yy, xx = np.mgrid[0:2400, 0:2400]
    ux = inv[0, 0] * xx + inv[0, 1] * yy + inv[0, 2]
    uy = inv[1, 0] * xx + inv[1, 1] * yy + inv[1, 2]
    scan = src[np.clip(np.round(uy), 0, 2399).astype(int),
               np.clip(np.round(ux), 0, 2399).astype(int)]
    F, resid = fit(src, scan)
    assert resid < 2.0, resid
    assert np.allclose(F[:, :2], M[:, :2], atol=0.002), F      # scale and rotation
    assert np.allclose(F[:, 2], M[:, 2], atol=2.0), F          # offset, to a pixel or two
    gx, gy, wx, wy = lattice(src, 8)
    X, Y = np.meshgrid(gx, gy)
    FX = F[0, 0] * X + F[0, 1] * Y + F[0, 2]
    FY = F[1, 0] * X + F[1, 1] * Y + F[1, 2]
    assert (wx, wy) == (4, 4), (wx, wy)
    was = samplexy(src, X, Y, wx, wy) < 128
    got = samplexy(scan, FX, FY, wx, wy) < 128
    bad = np.count_nonzero(was != got)
    assert bad * 100 < was.size, bad                 # a faithful copy reads back
    print(f"self-test ok: affine recovered under a 2.8% shrink, "
          f"{bad}/{was.size} cells differ on a clean copy")


if __name__ == '__main__':
    if sys.argv[1:2] == ['--self-test']:
        self_test()
    else:
        report(*sys.argv[1:3], float(sys.argv[3]),
               *(int(a) for a in sys.argv[4:5]))
