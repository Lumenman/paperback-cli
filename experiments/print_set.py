"""Generate a print/scan set with a manifest, so the pairs need no guessing later.

The existing scans lost their provenance: nobody remembers which sheet came
from which image or with which settings, which is why NOTES 25 has to begin by
matching block patterns. This writes the settings, the hashes and the file name
each scan must be saved under beside the sheets themselves.

    python experiments/print_set.py            # build sheets and manifest
    python experiments/print_set.py --verify   # re-check hashes of what is there

Sheets land in experiments/print-set/, which is ignored by git like the rest of
the material. Scans go back into experiments/print-set/scans/ under the names
the manifest gives.
"""
from pathlib import Path
import argparse, hashlib, random, subprocess, sys, tempfile

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / 'experiments' / 'print-set'
EXE = ROOT / ('paperback-cli.exe' if sys.platform == 'win32' else 'paperback-cli')
PRINT_DPI = 600                   # the image is rendered at the printer's resolution
INPUT_BYTES = 20000               # fits one sheet at every density below

# Each sheet answers one question. Keep the list short enough to actually print.
SHEETS = [
    ('01-d100-s70', ['-d', '100'],
     'Baseline. Must read. Gives the reference dot shape on this paper.'),
    ('02-d150-s70', ['-d', '150'],
     'The default. The sheet to scan twice, at 600 and at 300 dpi.'),
    ('03-d150-s70-header', ['-d', '150', '--header'],
     'The gray header and the footer under the grid, on paper for the first '
     'time: NOTES 16.1 only ever measured them on digital images.'),
    ('04-d175-s60', ['-d', '175', '-s', '60'],
     'Between the working density and the one NOTES 14 calls impossible, with '
     'a smaller dot to widen the gap between dots.'),
    ('05-d200-s70', ['-d', '200'],
     'NOTES 14 says this cannot work on this printer: 0.9 px of ink spread at '
     '600 dpi closes the gap. The sheet that tests that claim.'),
    ('06-d200-s50', ['-d', '200', '-s', '50'],
     'Same density, the smallest dot the encoder allows. If spread is the '
     'whole story this one reads and 05 does not.'),
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
    (OUT / 'scans').mkdir(exist_ok=True)
    source = OUT / 'input.bin'
    # Seeded, so the same bytes can be regenerated if the file is ever lost.
    source.write_bytes(random.Random(20260919).randbytes(INPUT_BYTES))
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
             f'no "enhance" or toner-saving mode. The images are rendered at {PRINT_DPI} dpi,',
             'so a printer set to that resolution puts one image pixel in one device dot.',
             'Print them all on the same paper from the same tray in one run.', '',
             '## Scanning', '',
             'Grayscale, **600 dpi**, no auto-crop, no deskew, no sharpening, no',
             'descreening, save as BMP. Lay the sheet straight; a few degrees of skew is',
             'fine, the decoder measures it, but keep the whole sheet on the glass.',
             '', 'Scan `02-d150-s70` a **second** time at 300 dpi, to separate what the',
             'printer loses from what the scanner loses.', '',
             'Save every scan in `experiments/print-set/scans/` under exactly the name in',
             'the table. That name is the whole point of this set: it is what the old',
             'scans lack.', '', '## Sheets', '']
    for name, page, printed, why, out, trip in rows:
        geometry = next((l for l in out if l.startswith('Sheet:')), '')
        lines += [f'### {name}', '', why, '',
                  f'- command: `{printed}`',
                  f'- sheet image: `{name}.bmp`, SHA-256 `{sha256(page)}`']
        if geometry: lines.append(f'- {geometry}')
        lines.append(f'- decodes as generated: {trip}, byte for byte')
        lines.append(f'- scan back as: `scans/{name}-600dpi.bmp`')
        if name.startswith('02'): lines.append('- and also: `scans/02-d150-s70-300dpi.bmp`')
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
    scans = sorted((OUT / 'scans').glob('*.bmp'))
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
