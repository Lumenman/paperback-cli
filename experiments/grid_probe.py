"""Where Getgridposition's 50%-of-maximum threshold puts the raster bounds.

Builds a separate decoder from a copy of src/Decoder.c with the contrast
profile dumped, so the shipped sources are not touched. Run from the
repository root: python experiments/grid_probe.py [extra.bmp ...]
"""
from pathlib import Path
import os, re, shutil, struct, subprocess, sys, tempfile

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'experiments'))
from grid_check import bmp, old_header, rotate, run          # same fixtures, one source

NHYST = 1024
SOURCES = ['paperbak.c', 'Printer.c', 'Scanner.c', 'Fileproc.c', 'Crc16.c',
           'Sha256.c', 'Text.c', 'Ecc.c']
DUMP = r'''
  {                                    // grid_probe: dump the profile and bounds
    const char *probe=getenv("PB_GRIDPROBE");
    FILE *pf=probe?fopen(probe,"w"):NULL;
    if (pf) {
      fprintf(pf,"size %d %d step %d %d n %d %d\n",sizex,sizey,stepx,stepy,nx,ny);
      fprintf(pf,"bounds %d %d %d %d\n",
        pdata->gridxmin,pdata->gridxmax,pdata->gridymin,pdata->gridymax);
      for (i=0; i<nx; i++) fprintf(pf,"x %d %d\n",i*stepx,distrx[i]);
      for (j=0; j<ny; j++) fprintf(pf,"y %d %d\n",j*stepy,distry[j]);
      fclose(pf);
    };
  }
  // Step finished.'''


def build(where):
    """A decoder identical to the shipped one except that it writes the profile."""
    where.mkdir(parents=True, exist_ok=True)
    text = (ROOT / 'src' / 'Decoder.c').read_text(encoding='utf-8')
    marker = '  // Step finished.'
    head, _, rest = text.partition(marker)          # first occurrence is in Getgridposition
    assert rest, 'marker not found in Decoder.c'
    (where / 'Decoder.c').write_text(head + DUMP + rest, encoding='utf-8')
    exe = where / ('grid-probe' + ('.exe' if os.name == 'nt' else ''))
    cmd = ['gcc', '-Iinclude', '-Ilib/PortLibC/include', '-O2', '-std=gnu11',
           'src/main.c', str(where / 'Decoder.c')]
    cmd += [f'src/{s}' for s in SOURCES]
    cmd += ['lib/PortLibC/src/FileAttributes.c', 'lib/PortLibC/src/Borland.c',
            '-lm', '-o', str(exe)]
    subprocess.run(cmd, cwd=ROOT, check=True)
    return exe


def darkbox(path, limit=64):
    """Bounding box of the dots, in decoder coordinates (row 0 is the bottom).

    Gray header text is 128 and stays out of it; on a page whose text is black
    the box is taken from the gray original instead, since only the text moved.
    """
    b, w, h, off, stride = bmp(path)
    xmin, xmax, ymin, ymax = w, -1, h, -1
    for y in range(h):                               # file order: row 0 is the bottom
        row = b[off + y * stride:off + y * stride + w]
        if min(row) >= limit: continue
        ymin = min(ymin, y); ymax = max(ymax, y)
        xmin = min(xmin, next(x for x, v in enumerate(row) if v < limit))
        xmax = max(xmax, w - 1 - next(x for x, v in enumerate(reversed(row)) if v < limit))
    return None if ymax < 0 else (xmin, xmax, ymin, ymax)


def mirrored(box, w, h):
    xmin, xmax, ymin, ymax = box
    return (w - 1 - xmax, w - 1 - xmin, h - 1 - ymax, h - 1 - ymin)


def probe(exe, page, out):
    dump = out.parent / 'profile.txt'
    dump.unlink(missing_ok=True)
    env = dict(os.environ, PB_GRIDPROBE=str(dump))
    out.unlink(missing_ok=True)
    p = subprocess.run([str(exe), '--decode', '--force', '-i', str(page), '-o', str(out)],
                       capture_output=True, text=True, timeout=900, env=env, cwd=ROOT)
    if not dump.exists(): return None, p
    size = step = bounds = None
    xs, ys = [], []
    for line in dump.read_text().splitlines():
        f = line.split()
        if f[0] == 'size': size = (int(f[1]), int(f[2])); step = (int(f[4]), int(f[5]))
        elif f[0] == 'bounds': bounds = tuple(map(int, f[1:5]))
        elif f[0] == 'x': xs.append((int(f[1]), int(f[2])))
        elif f[0] == 'y': ys.append((int(f[1]), int(f[2])))
    return dict(size=size, step=step, bounds=bounds, xs=xs, ys=ys), p


def window(bounds, size):
    """The 1024x1024 search window Getgridintensity centres between the bounds."""
    xmin, xmax, ymin, ymax = bounds
    sizex, sizey = size
    out = []
    for lo, hi, extent in ((xmin, xmax, sizex), (ymin, ymax, sizey)):
        c = (lo + hi) // 2
        a = max(c - NHYST // 2, 0); b = min(a + NHYST, extent)
        out += [a, b]
    return out                                        # x0,x1,y0,y1


def overlap(win, box):
    """Share of the search window that actually holds raster.

    Zero is fatal: Getgridintensity measures ink and paper inside this window
    and refuses the sheet when it finds no contrast there.
    """
    x0, x1, y0, y1 = win
    bx0, bx1, by0, by1 = box
    dx = max(0, min(x1, bx1 + 1) - max(x0, bx0))
    dy = max(0, min(y1, by1 + 1) - max(y0, by0))
    return dx * dy / max(1, (x1 - x0) * (y1 - y0))


def reach(profile, lo, hi, step):
    """How the samples on the grid compare with the samples off it.

    Returns (peak off the grid, median on it) as fractions of the profile
    maximum. A sample within one sampling step of the box counts as on the
    grid, since that is the resolution the bound is found at. Threshold is
    half the maximum, so an off-grid peak above twice the on-grid median is
    enough to drag the bound away from the raster.
    """
    inside = sorted(v for pos, v in profile if lo - step <= pos <= hi + step)
    outside = [v for pos, v in profile if not lo - step <= pos <= hi + step]
    top = max(v for _, v in profile) or 1
    return (max(outside) / top if outside else 0.0,
            inside[len(inside) // 2] / top if inside else 0.0)


# Candidate rules for turning a contrast profile into one bound pair. The
# shipped rule is "first and last sample at or above half the maximum"; the
# rest are here to be measured against it, not because they are better.
def rule_firstlast(profile, frac):
    top = max(v for _, v in profile) or 1
    hits = [pos for pos, v in profile if v >= top * frac]
    return (hits[0], hits[-1]) if hits else None


def rule_longestrun(profile, frac):
    """The widest uninterrupted stretch above the threshold.

    A raster is a long run; a band of text is a short one, however dark.
    """
    top = max(v for _, v in profile) or 1
    best = run = None
    for pos, v in profile:
        if v >= top * frac:
            run = (run[0], pos) if run else (pos, pos)
            if best is None or run[1] - run[0] > best[1] - best[0]: best = run
        else:
            run = None
    return best


RULES = [('half max, first..last (shipped)', lambda pr: rule_firstlast(pr, 0.5)),
         ('half max, longest run', lambda pr: rule_longestrun(pr, 0.5)),
         ('third of max, longest run', lambda pr: rule_longestrun(pr, 1 / 3)),
         ('quarter of max, longest run', lambda pr: rule_longestrun(pr, 0.25))]


def rulecover(info, box, rule):
    """Share of raster in the window each rule would end up measuring."""
    x = rule(info['xs']); y = rule(info['ys'])
    if not x or not y: return 0.0
    return overlap(window((x[0], x[1], y[0], y[1]), info['size']), box)


def report(name, info, box, p):
    if info is None:
        print(f'{name:<22} no profile (decode did not reach the raster search)')
        return
    b = info['bounds']; win = window(b, info['size'])
    stepx, stepy = info['step']
    if box is not None:
        xpeak, xmed = reach(info['xs'], box[0], box[1], stepx)
        ypeak, ymed = reach(info['ys'], box[2], box[3], stepy)
    got = re.search(r'Recovered (\d+)/(\d+)', p.stdout)
    verdict = f'{got[1]}/{got[2]}' if got else (p.stderr.strip().splitlines() or ['-'])[0][:18]
    if box is None:
        # No ground truth for a scan: show what each rule would pick instead.
        print(f'{name:<22} {verdict}')
        for label, r in RULES:
            x = r(info['xs']); y = r(info['ys'])
            if not x or not y:
                print(f'    {label:<32} nothing above the threshold')
                continue
            print(f'    {label:<32} x {x[0]:>5}..{x[1]:<6} y {y[0]:>5}..{y[1]:<6}'
                  f'  of {info[chr(115)+chr(105)+chr(122)+chr(101)][0]}x{info["size"][1]}')
        return
    err = (b[0] - box[0], b[1] - box[1], b[2] - box[2], b[3] - box[3])
    cover = overlap(win, box)
    covers = ' '.join(f'{rulecover(info, box, r):5.2f}' for _, r in RULES)
    flag = '  <== WINDOW OFF THE GRID' if cover == 0 else ''
    print(f'{name:<22} err {err[0]:>+5}{err[1]:>+6}{err[2]:>+6}{err[3]:>+6} '
          f'| rows on/off {ymed:4.2f}/{ypeak:4.2f} '
          f'| raster in window: {covers} | {verdict}{flag}')
    return [rulecover(info, box, r) for _, r in RULES]


if __name__ == '__main__':
    extra = [Path(a).resolve() for a in sys.argv[1:]]
    build_dir = ROOT / 'experiments' / 'grid-probe-build'
    exe = build(build_dir)
    with tempfile.TemporaryDirectory(prefix='grid-probe-', dir='.') as folder:
        root = Path(folder)
        cases, _ = __import__('grid_check').fixtures(root, exe)
        out = root / 'out.bin'
        truth = {}
        print('decoder coordinates; row 0 is the bottom of the sheet\n')
        print('rules:', ', '.join(n for n, _ in RULES))
        print()
        scores = []
        for paper in ['A4', 'Letter', 'A5', 'A3', 'A6', 'Legal', 'Tabloid']:
            page = root / (paper + '.bmp')
            box = darkbox(page); truth[paper] = box
            info, p = probe(exe, page, out)
            scores.append(report(paper + ' gray', info, box, p))
        print()
        for name, page, _ in cases:
            paper = name.split('-')[0]
            box = truth.get(paper)
            if box and name.endswith('-180'):
                _, w, h, _, _ = bmp(page); box = mirrored(box, w, h)
            elif paper not in truth:
                box = darkbox(page)
            info, p = probe(exe, page, out)
            got = report(name + (' legacy' if paper in truth else ''), info, box, p)
            if got and name not in ('blank', 'noise', 'text-only'): scores.append(got)
        print()
        print('pages whose window holds no raster, by rule:')
        for i, (label, _) in enumerate(RULES):
            print(f'  {sum(1 for s in scores if s and s[i] == 0):>2} of {len(scores)}  {label}')
        print()
        for page in extra:
            info, p = probe(exe, page, out)
            report(page.name, info, None, p)
