"""Do two scans lose the same dots, or different ones?

The question under "would voting between several reads help": a vote only
beats noise that falls in a different place each time. Reuses dot_diff for
everything hard - the affine fit, the lattice, the per-tile shift, the
threshold - and only asks the extra question, so both masks are per-cell
verdicts on the SOURCE lattice and line up cell for cell.

  python experiments/vote_corr.py SOURCE.bmp SCAN_A.bmp SCAN_B.bmp CELLPX
  python experiments/vote_corr.py --self-test

Independent damage gives a coincidence ratio near 1; the same dots failing
twice gives the ratio by which a vote is worth less than it looks.
"""
import os
import sys
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dot_diff as dd


def mask(source, scanfile, cell, tile=32):
    """Per-cell "read wrong" mask on the source lattice, and what was inked.

    dot_diff.report's own body, minus the printing: it stops at the numbers
    and the mask is what this script needs."""
    src, scan = dd.load(source), dd.load(scanfile)
    box, sbox = dd.inkbox(src), dd.inkbox(scan)
    M, resid = dd.fit(src, scan, box, sbox)
    aligned = dd.warp(scan, np.linalg.inv(dd.square(M))[:2])
    gx, gy, wx, wy = dd.lattice(src, cell, box)
    gx, gy = gx[:len(gx) // tile * tile], gy[:len(gy) // tile * tile]
    X, Y = np.meshgrid(gx, gy)
    ink = dd.samplexy(src, X, Y, wx, wy) < 128
    SX, SY = dd.localshifts(aligned, X, Y, ink, wx, wy, tile)
    got = dd.samplexy(aligned, X + SX, Y + SY, wx, wy)
    _, first = min((np.count_nonzero((got < v) != ink), v) for v in range(20, 250, 2))
    SX, SY = dd.localshifts(aligned, X, Y, ink, wx, wy, tile, limit=first)
    got = dd.samplexy(aligned, X + SX, Y + SY, wx, wy)
    err, t = min((np.count_nonzero((got < t) != ink), t) for t in range(20, 250, 2))
    print(f"  {scanfile}: {err}/{ink.size} wrong = {100.0 * err / ink.size:.3f}%, "
          f"threshold {t}, residual {resid:.1f} px")
    return (got < t) != ink, ink


def stats(wa, wb):
    """How much more often than chance the two masks fail on the same cell."""
    n = wa.size
    na, nb = np.count_nonzero(wa), np.count_nonzero(wb)
    both = np.count_nonzero(wa & wb)
    chance = na * nb / n                 # if the two were independent
    return dict(n=n, a=na, b=nb, both=both, chance=chance,
                ratio=both / chance if chance else float('inf'),
                conditional=both / na if na else 0.0,
                phi=np.corrcoef(wa.ravel().astype(float),
                                wb.ravel().astype(float))[0, 1],
                onlya=np.count_nonzero(wa & ~wb), onlyb=np.count_nonzero(~wa & wb))


def compare(source, a, b, cell):
    print(source)
    wa, _ = mask(source, a, cell)
    wb, _ = mask(source, b, cell)
    s = stats(wa, wb)
    print(f"  wrong in both: {s['both']} cells; if independent: {s['chance']:.1f}"
          f"  -> {s['ratio']:.1f}x")
    print(f"  P(B wrong | A wrong) = {s['conditional']:.4f}, "
          f"base P(B wrong) = {s['b'] / s['n']:.4f}")
    print(f"  phi correlation {s['phi']:.3f}")
    # Not a vote - two readers cannot outvote each other - but its ceiling: a
    # third reader can only help where these two already disagree.
    print(f"  wrong in exactly one: {s['onlya']} + {s['onlyb']} cells, "
          f"which is what a tie-breaker could reach")


def self_test():
    """The ratio is the whole measurement, so pin it at both ends."""
    rng = np.random.default_rng(3)
    a = rng.random((400, 400)) < 0.05
    b = rng.random((400, 400)) < 0.05
    assert 0.8 < stats(a, b)['ratio'] < 1.2, stats(a, b)      # independent: chance
    assert abs(stats(a, b)['phi']) < 0.05, stats(a, b)
    assert np.isclose(stats(a, a)['ratio'], 1.0 / a.mean()), stats(a, a)  # identical: 1/p
    assert stats(a, a)['conditional'] == 1.0
    assert stats(a, ~a)['both'] == 0 and stats(a, ~a)['ratio'] == 0.0
    print("self-test ok: chance reads as 1x, the same mask twice as 1/p")


if __name__ == '__main__':
    if sys.argv[1:2] == ['--self-test']:
        self_test()
    else:
        compare(sys.argv[1], sys.argv[2], sys.argv[3], float(sys.argv[4]))
