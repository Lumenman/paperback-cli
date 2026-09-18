"""Simulate print+scan of a page: dot gain, MTF blur, drift, skew, noise.

`mtf=1` replaces the guessed `blur` with the PSF measured on real paper by the
grating strip of the paper-sound project (NOTES.md 8.1): nine printed gratings
of known period, read back, fitted to A*exp(-2pi^2 s^2 / lam^2) -> A=0.919,
s=1.077 px at 600 dpi. A<1 is a wide halo (toner scatter, glass flare) already
flat at a 20 px period, so it is modelled as a second, much wider gaussian.
Same laser printer + flatbed pair, so this is the number to believe over any
blur= we invented. mtf= scales sigma, to ask what a worse scanner would do.
"""
import sys
import numpy as np
from PIL import Image, ImageFilter


MTF_SIGMA = 1.077   # px at 600 dpi, measured
MTF_HALO = 0.081    # 1 - A: contrast the halo costs at every period
MTF_HALO_SIGMA = 10.0


def simulate(src, dst, ratio=2, gain=0.0, blur=0.8, skew=0.0, drift=0.0,
             noise=0.0, seed=1, mtf=0.0, dpi=0):
    img = Image.open(src).convert('L')
    if mtf:
        dpi = dpi or img.info.get('dpi', (600,))[0] or 600
        blur = 0.0
    a = np.asarray(img).astype(np.float32)
    # Dot gain: ink spreads, black grows into its neighbours.
    if gain > 0:
        b = Image.fromarray(a.astype(np.uint8)).filter(
            ImageFilter.GaussianBlur(gain))
        a = np.minimum(a, np.asarray(b).astype(np.float32) + (255 - 255 * gain))
    img = Image.fromarray(np.clip(a, 0, 255).astype(np.uint8))
    # Paper skew on the glass.
    if skew:
        img = img.rotate(skew, resample=Image.BILINEAR, fillcolor=255)
    # Feed drift: local scale runs 1-drift .. 1+drift down the page.
    if drift:
        h = img.height
        y = np.arange(h, dtype=np.float32)
        t = y / h
        src_y = np.clip(y + drift * h * (t * t - t), 0, h - 1.001)
        a = np.asarray(img).astype(np.float32)
        i = src_y.astype(int)
        f = (src_y - i)[:, None]
        a = a[i] * (1 - f) + a[i + 1] * f
        img = Image.fromarray(np.clip(a, 0, 255).astype(np.uint8))
    # Scanner optics, then sampling at the scan resolution.
    if mtf:
        k = dpi / 600.0
        ink = 255.0 - np.asarray(img).astype(np.float32)
        def g(s):
            return 255.0 - np.asarray(Image.fromarray(
                (255 - ink).astype(np.uint8)).filter(
                    ImageFilter.GaussianBlur(s))).astype(np.float32)
        ink = (1 - MTF_HALO) * g(MTF_SIGMA * mtf * k) + MTF_HALO * g(
            MTF_HALO_SIGMA * k)
        img = Image.fromarray(np.clip(255.0 - ink, 0, 255).astype(np.uint8))
    elif blur:
        img = img.filter(ImageFilter.GaussianBlur(blur * ratio))
    img = img.resize((img.width // ratio, img.height // ratio), Image.BOX)
    a = np.asarray(img).astype(np.float32)
    if noise:
        a += np.random.default_rng(seed).normal(0, noise, a.shape)
    Image.fromarray(np.clip(a, 0, 255).astype(np.uint8)).save(dst)


if __name__ == '__main__':
    kw = dict(p.split('=') for p in sys.argv[3:])
    simulate(sys.argv[1], sys.argv[2],
             **{k: (int(v) if k in ('ratio', 'seed', 'dpi') else float(v))
                for k, v in kw.items()})
