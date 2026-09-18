"""Check scan.py's mtf=1 PSF against the contrasts measured on paper.

    python experiments/mtf_check.py

Nine bar gratings of the periods the grating strip printed (NOTES.md 8.1),
pushed through the simulator, fundamental measured against the same grating
unblurred. The model
is one gaussian plus a halo where the paper wanted something with a sharper
core and a wider skirt, so it is allowed 0.06 of contrast either way -- enough
to catch the kernel being wrong, not enough to demand a PSF the fit never had.
"""
import os
import sys
import tempfile

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from scan import simulate

MEASURED = {38.7: 0.956, 19.7: 0.815, 13.9: 0.761, 9.9: 0.690, 7.9: 0.627,
            6.1: 0.535, 4.9: 0.400, 4.1: 0.251, 3.4: 0.136, 2.9: 0.053}
TOL = 0.06


def contrast(lam, tmp):

    h, w = 256, 2048
    duty = 4.0 / 38.7 if lam == 38.7 else 0.5  # the reference block is a stroke
    bars = (np.arange(w) / lam) % 1.0 < duty
    Image.fromarray(np.tile(np.where(bars, 0, 255).astype(np.uint8), (h, 1))
                    ).save(tmp + "g.bmp")
    simulate(tmp + "g.bmp", tmp + "go.bmp", ratio=1, blur=0, mtf=1, dpi=600)

    def fund(path):
        a = 255.0 - np.asarray(Image.open(path).convert("L"), dtype=float)
        p = a[h // 4:3 * h // 4].mean(0)[64:-64]
        F = np.abs(np.fft.rfft(p - p.mean()))
        k = int(round(len(p) / lam))
        return F[max(k - 2, 0):k + 3].max()

    return fund(tmp + "go.bmp") / fund(tmp + "g.bmp")


if __name__ == "__main__":
    tmp = tempfile.mkdtemp() + os.sep
    bad = 0
    for lam, want in sorted(MEASURED.items()):
        got = contrast(lam, tmp)
        off = abs(got - want)
        bad += off > TOL
        print(f"lambda {lam:5.1f} px   model {got:.3f}   paper {want:.3f}"
              f"   {'off by %.3f' % off if off > TOL else 'ok'}")
    assert not bad, f"{bad} periods off the measured MTF by more than {TOL}"
    print("ok")
