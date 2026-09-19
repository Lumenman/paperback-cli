"""Generate a print/scan set with a manifest, so the pairs need no guessing later.

The existing scans lost their provenance: nobody remembers which sheet came
from which image or with which settings, which is why NOTES 25 has to begin by
matching block patterns. This writes the settings, the hashes and the file name
each scan must be saved under beside the sheets themselves.

    python experiments/print_set.py            # build sheets and manifest
    python experiments/print_set.py --verify   # re-check hashes of what is there

Sheets land in experiments/print-set/, which is ignored by git like the rest of
the material. Scans go back into experiments/print-set/<round>/ under the names
the manifest gives.
"""
from pathlib import Path
import argparse, hashlib, os, random, subprocess, sys, tempfile

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / 'experiments' / 'print-set'
EXE = ROOT / ('paperback-cli.exe' if sys.platform == 'win32' else 'paperback-cli')
PRINT_DPI = 600                   # the image is rendered at the printer's resolution
INPUT_BYTES = 20000               # fits one sheet at every density below
INPUT_MTIME = 1757000000          # pinned: the sheet carries the input's mtime
# One folder per print run. Two rounds of the same sheets under one name would
# overwrite each other, and a scan whose round is unknown is the provenance
# problem this whole set exists to avoid.
SCANS = 'scans3'                  # round 1 'scans', round 2 'scans2'

# Each sheet answers one question. Keep the list short enough to actually print.
SHEETS = [
    ('01-d100-s70', ['-d', '100'],
     'Reference. Read in both earlier rounds, so it says what this paper and '
     'this toner do on a sheet that is never in doubt.'),
    ('02-d150-s70', ['-d', '150'],
     'The default, and the working point since the printer sharpens (NOTES 21). '
     'Scanned twice, at 600 and at 300 dpi: at 300 it read 146 blocks of 223, '
     'so that is where the margin can be seen shrinking.'),
    ('03-d150-s100', ['-d', '150', '-s', '100'],
     'The dot fills its cell: no gap between dots at all. NOTES 14 reasoned '
     'from the gap closing, NOTES 21.3 found darkness decides and the gap does '
     'not. If 21.3 is right this is the best sheet of the set, and if it is '
     'wrong this is the worst.'),
    ('04-d120-s70', ['-d', '120'],
     'At 600 dpi a cell is a whole number of pixels, so five pixels is the only '
     'density there is between 100 and 150.'),
    ('05-d150-s50', ['-d', '150', '-s', '50'],
     'The smallest dot at the working density. With 02 and 03 this makes three '
     'points of one curve - two, three and four pixels of dot in the same four '
     'pixel cell - instead of three anecdotes.'),
    ('06-d100-s50', ['-d', '100', '-s', '50'],
     'The smallest dot at the density that always reads, where every wrong dot '
     'can be counted. Separates dot size from density; against 01 it is the '
     'same question as 05 against 02, one density lower.'),
]


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def version():
    for args in (['git', 'describe', '--always', '--dirty'], ['git', 'rev-parse', 'HEAD']):
        p = subprocess.run(args, cwd=ROOT, capture_output=True, text=True)
        if p.returncode == 0: return p.stdout.strip()
    return 'unknown'


def build():
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / SCANS).mkdir(exist_ok=True)
    source = OUT / 'input.bin'
    # Seeded, so the same bytes can be regenerated if the file is ever lost.
    data = random.Random(20260919).randbytes(INPUT_BYTES)
    if not source.exists() or source.read_bytes() != data:
        source.write_bytes(data)
    # The superblock carries the input file's modification time, so a sheet
    # built from the same bytes at a different minute is a different bitmap.
    # Pinning the mtime makes the sheets reproducible byte for byte, which is
    # what lets a scan be compared against the sheet that produced it.
    os.utime(source, (INPUT_MTIME, INPUT_MTIME))
    rows = []
    for name, opts, why in SHEETS:
        page = OUT / (name + '.bmp')
        for stale in OUT.glob(name + '*.bmp'): stale.unlink()
        cmd = [str(EXE), '--encode', '-i', str(source), '-o', str(page),
               '--image-dpi', str(PRINT_DPI), *opts]
        p = subprocess.run(cmd, capture_output=True, text=True)
        assert p.returncode == 0, (name, p.stderr)
        extra = sorted(OUT.glob(name + '_*.bmp'))
        assert not extra, f'{name} needed {len(extra)} sheets; lower INPUT_BYTES'
        assert page.exists(), name
        printed = ' '.join(['paperback-cli', '--encode', '-i', 'input.bin',
                            '-o', page.name, '--image-dpi', str(PRINT_DPI), *opts])
        rows.append((name, page, printed, why, p.stdout.strip().splitlines(),
                     roundtrip(page, source)))
    # Two sheets with the same SHA-256 are the same print, and the questions
    # they were supposed to separate stay unanswered - at 600 dpi a cell must be
    # a whole number of pixels, so -d 175 becomes 200 and every -s from 50 to 70
    # gives the same two-pixel dot. Cheaper to notice here than after printing
    # three copies of one sheet (NOTES.md 20.6).
    same = {}
    for name, page, *_ in rows:
        same.setdefault(sha256(page), []).append(name)
    twins = [v for v in same.values() if len(v) > 1]
    assert not twins, f'identical sheets, differing only in the options asked for: {twins}'
    return source, rows


def roundtrip(page, source):
    """Decode the sheet as generated, before any paper is involved.

    A sheet that already fails here would waste a print, and knowing it passed
    is what makes a failure after printing evidence about print and scan.
    """
    with tempfile.TemporaryDirectory(prefix='print-set-') as folder:
        out = Path(folder) / 'out.bin'
        p = subprocess.run([str(EXE), '--decode', '-i', str(page), '-o', str(out)],
                           capture_output=True, text=True)
        got = next((l for l in p.stdout.splitlines() if l.startswith('Recovered')), '')
        blocks = got.split(';')[0].replace('Recovered ', '') if got else 'nothing'
        assert out.exists() and out.read_bytes() == source.read_bytes(), (
            f'{page.name} does not decode as generated: {blocks}')
        return blocks


def manifest(source, rows):
    lines = ['# Print/scan set', '',
             f'Built by `experiments/print_set.py` from paperback-cli `{version()}`.',
             '', f'Input: `input.bin`, {INPUT_BYTES} bytes, SHA-256 `{sha256(source)}`.',
             'All sheets carry the same bytes, so a block that reads on one sheet and',
             'not on another differs by printing, not by content.', '',
             '## Printing', '',
             f'Print each `.bmp` at **actual size (100%)**, no fit-to-page, no scaling,',
             f'no toner-saving mode. The images are rendered at {PRINT_DPI} dpi, so a',
             'printer set to that resolution puts one image pixel in one device dot.',
             'Print them all on the same paper from the same tray in one run.', '',
             "Leave the printer's own sharpness enhancement **on**. It was off for the",
             'first round of this set and on for the second, and that one setting is the',
             'difference between 4 blocks of 223 and all 223 at the default density: it',
             'lays down a darker dot, and a pale dot read as paper is what costs the',
             'sheet (NOTES.md 21). Sharpening in the *scanner* is a different thing and',
             'still has to be off - there it throws away the partial pixels at the edge',
             'of a dot, which is what the decoder measures with.', '',
             '## Scanning', '',
             'Grayscale, **600 dpi**, no auto-crop, no deskew, no sharpening, no',
             'descreening, save as BMP. Lay the sheet straight; a few degrees of skew is',
             'fine, the decoder measures it, but keep the whole sheet on the glass.',
             '', 'Scan `02-d150-s70` a **second** time at 300 dpi, to separate what the',
             'printer loses from what the scanner loses.', '',
             f'Save every scan in `experiments/print-set/{SCANS}/` under exactly the name',
             'in',
             'the table. That name is the whole point of this set: it is what the old',
             'scans lack.', '', '## Sheets', '']
    for name, page, printed, why, out, trip in rows:
        geometry = next((l for l in out if l.startswith('Sheet:')), '')
        lines += [f'### {name}', '', why, '',
                  f'- command: `{printed}`',
                  f'- sheet image: `{name}.bmp`, SHA-256 `{sha256(page)}`']
        if geometry: lines.append(f'- {geometry}')
        lines.append(f'- decodes as generated: {trip}, byte for byte')
        lines.append(f'- scan back as: `{SCANS}/{name}-600dpi.bmp`')
        if name.startswith('02'):
            lines.append(f'- and also: `{SCANS}/02-d150-s70-300dpi.bmp`')
        lines.append('')
    lines += ['## After scanning', '',
              'Every sheet holds the same known bytes, and for a sheet whose raster is',
              'found the decoder knows each block\'s origin, step and angle. That is',
              'enough to compare the dots as read against the bits as encoded, which is',
              'the measurement NOTES 25 wants and the old scans cannot support.', '',
              'Sheets that fail to decode entirely are still worth keeping: with the',
              'settings recorded, a failure is evidence rather than a mystery.',
              'Every sheet here decodes byte for byte as generated, so whatever a scan',
              'loses was lost between the printer and the scanner glass.', '']
    (OUT / 'MANIFEST.md').write_text('\n'.join(lines), encoding='utf-8')


def verify():
    text = (OUT / 'MANIFEST.md').read_text(encoding='utf-8')
    missing = bad = 0
    for line in text.splitlines():
        if '`, SHA-256 `' not in line: continue
        name = line.split('`')[1]; want = line.split('`')[3]
        path = OUT / name
        if not path.exists(): print(f'missing {name}'); missing += 1
        elif sha256(path) != want: print(f'CHANGED {name}'); bad += 1
    scans = sorted((OUT / SCANS).glob('*.bmp'))
    print(f'{len(SHEETS)} sheets, {missing} missing, {bad} changed; {len(scans)} scans present')
    for s in scans: print(f'  {s.name}  {sha256(s)[:16]}  {s.stat().st_size} bytes')


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--verify', action='store_true', help='re-check hashes instead of building')
    if ap.parse_args().verify:
        verify()
    else:
        assert EXE.exists(), f'build the CLI first: {EXE}'
        source, rows = build()
        manifest(source, rows)
        print(f'{len(rows)} sheets and MANIFEST.md in {OUT}')
        for name, page, _, _, _, _ in rows:
            print(f'  {name}.bmp  {page.stat().st_size // 1024} KiB')
