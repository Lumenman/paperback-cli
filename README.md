# paperback-cli

Cross-platform command-line version of Oleh Yuschuk's PaperBack. It encodes files into printable BMP pages and restores files from scans. Files are stored as original bytes. Legacy PaperBack pages using internal compression or encryption are not supported. No AES or bzip2 dependencies are used.

## Build and check

Use a modern GCC/MinGW-w64 compiler and GNU Make:

```sh
make
make test
```

Every dependency is in this repository; no submodule or package has to be fetched. The checks also need Python 3 (standard library only); the Makefile calls `python` on Windows and `python3` elsewhere. On Windows, ensure the MinGW-w64 `bin` directory comes before any other compiler on PATH. You can run the integration checks directly with `python test_cli.py ./paperback-cli.exe`.

Builds and passes its checks with MinGW-w64 GCC on Windows and with GCC on Linux.

## Encode a full sheet

```sh
paperback-cli --encode -i backup.bin -o sheet.bmp --paper A4 --margin 10mm
paperback-cli --encode -i backup.bin -o sheet.bmp --paper Letter --landscape
paperback-cli --encode -i backup.bin -o sheet.bmp --paper-size 210x297mm --margin 10mm --margin-left 15mm
```

Presets: **A3, A4, A5, A6, Letter, Legal, Tabloid**, case-insensitive. A4 is the default. Custom dimensions accept `WxHmm` or `WxHin`; bare numbers mean millimetres. `--landscape` swaps the selected dimensions. Margins accept `mm` or `in`; default is 10 mm on every side. `--margin` sets all four; individual `--margin-left`, `--margin-right`, `--margin-top`, `--margin-bottom` options override them in command-line order.

Every BMP is a complete white sheet, with the data grid placed inside the margins. The last page has the same physical dimensions as the others. A multi-page backup is named `sheet_0001.bmp`, `sheet_0002.bmp`, etc.; a one-page backup uses `sheet.bmp`. BMP resolution metadata records the physical size. **Print at 100% / actual size**, with fitting, cropping and automatic enlargement disabled. Your print application must respect the BMP resolution metadata; the CLI does not control printer settings. On Linux each page is written with mode 0600, since it holds the same bytes as the input file.

`--dpi` is the density of code dots (40–600, default 200). `--image-dpi` is the bitmap resolution (80–2400), defaulting to three times the dot density. It must be at least twice the requested dot density. Dot spacing is rounded to whole pixels; the CLI reports the resulting density. `--dotsize` is the dot width as a percentage of spacing (50–100, default 70), also rounded to pixels. Large page/resolution combinations exceeding 32768 pixels per side or approximately 256 MiB of sheet pixels are rejected.

`--redundancy N` adds one recovery block per N data blocks (2–10, default 5). **Smaller N provides more redundancy** and less capacity. `--border` enables the outer black border. Text headers/footers are not implemented; `--no-header` remains a compatibility no-op. Create archives or encrypted files using external tools before encoding. The CLI treats them as ordinary bytes and never unpacks or decrypts them.

## Restore from one or more scans

```sh
paperback-cli --decode -i scan.bmp -o restored.bin
paperback-cli --decode -o restored.bin scan1.bmp scan2.bmp scan3.bmp
paperback-cli --decode -i scan1.bmp -i scan2.bmp -o restored.bin
paperback-cli --decode -i scan.bmp -p 4 -o restored.bin
```

The last command reads `scan_0001.bmp` through `scan_0004.bmp`. A numbered sequence accepts one input basename. Other forms accept arbitrary scan paths. Input is uncompressed 8-bit paletted or 24-bit BMP, including top-down BMPs; convert PNG/JPEG/TIFF scans first.

Scans may be repeated, supplied out of order, or rotated by 180 degrees. Rotation by multiples of 90 degrees and mirroring are detected by the decoder; small skew is also supported. Successfully decoded blocks and recovery blocks accumulate across all scans in a single invocation. Repeated scans can fill each other's gaps. This combines decoded blocks, not image pixels. Scans identified as different files cannot be written to the same output.

Output is saved after processing the entire input list. The summary reports recovered/missing blocks and, when page geometry is consistent, page numbers to rescan. A readable page header is required to associate its blocks with a file. Completely unreadable pages cannot contribute data. On Linux the restored file and its `.map` are set to mode 0600: a page stores a single attribute bit, so original permissions cannot be reproduced and are never widened.

## Accept damaged pages with --force

```sh
paperback-cli --decode --force -o restored.bin damaged.bmp rescan.bmp
```

`--force` means **accept damaged pages and keep processing the remaining scans**. Recoverable blocks still need to pass block ECC/CRC checks. It does not mark corrupt bytes as valid. Truncated BMP pixel data and invalid palette indices are treated as white where possible; unsafe or unsupported headers are rejected and skipped. Without `--force`, an input read/format/decoder error stops the run. A readable page with missing blocks can still be supplemented by later scans without this option.

After all scans, the result always uses the exact path passed to `-o`, preserving the original length and byte offsets. No `.partial` or `.raw.partial` suffix is added.

- A complete file with a matching checksum is saved normally.
- With `--force`, missing byte ranges are filled with zeros. Later readable bytes remain at their original offsets.
- With `--force`, a file checksum mismatch also allows saving: readable bytes are kept unchanged. A checksum mismatch alone cannot identify the damaged ranges, and the map says so.
- Every saved file has an `OUTPUT.map` sidecar recording status, checksum result, original size, recovered block count and missing ranges (`start inclusive`, `end exclusive`, offsets in the original file). The map is rewritten on every save, including a later complete restore, so it cannot retain stale damage ranges.

For example, `--decode --force -o backup.rar damaged.bmp` creates `backup.rar` and `backup.rar.map`. You can pass the archive to an external archiver to attempt recovery using its recovery records. This program does not repair the archive structure. Gaps are ordinary zero bytes; filesystem sparse-file support is not required.

An entirely unreadable file header or scans belonging to different files still cause failure; `--force` cannot invent file metadata. Legacy internally compressed/encrypted pages are rejected even with `--force`. An external archive or encrypted file is supported as plain file bytes but may remain unusable after partial recovery.

Exit status: **0** complete verified output, **1** failure (including output/map write errors), **2** damaged output saved. A complete verified result after skipping bad scans with `--force` returns 0; diagnostics remain visible. File CRC is checked even when its recorded value is zero; the old CLI exception for absent CRC is removed.

## License and origin

This program is free software under the **GNU General Public License version 3**; see `LICENSE`. It comes with no warranty. Test a print/scan/restore cycle with your actual printer and scanner before relying on paper backups.

Forked from Oleh Yuschuk's [PaperBack](http://www.ollydbg.de/Paperbak/index.html), whose sources are licensed under the GPL version 2 or, at your option, any later version, with command-line contributions by scuti and surkeh. The Reed-Solomon implementation in `src/Ecc.c` is derived from software by Phil Karn KA9Q, copyright 2002, under the GPL. Keep the copyright and license headers in the source files intact when you redistribute this code.

### Modifications

This is a modified version of paperback-cli. Changes against the upstream project, last updated 2026-09-17:

- Stored data is the original file bytes. Internal compression and encryption are gone, and with them the AES and bzip2 dependencies. Legacy pages that use them are rejected.
- Page geometry is selectable: paper presets, custom sheet sizes, per-side margins, dot density and bitmap resolution. Every page is a complete printable sheet.
- Damaged scans are handled by `--force`, which writes the output to the exact path given, fills unreadable ranges with zeros and records them in an `OUTPUT.map` sidecar.
- Block decoding retries with erasures: the 32 least reliable bytes of a failed block are located for the Reed-Solomon code, which corrects 32 bytes of known position against 16 of unknown position. A stripe eight dot rows wide, faded to the black/white threshold, is now read where blocks were previously lost.
- The dot overlap correction in the decoder sampled the transposed cell of the block; it now reads the cell it corrects.
- `lib/PortLibC` is a plain directory in this repository instead of a git submodule.

### Bundled libraries

`lib/PortLibC` is copyright surkeh and licensed under the **GNU Lesser General Public License version 3**; its license text stays in `lib/PortLibC/LICENSE`. It came from `https://git.teknik.io/suhrke/PortLibC.git`, which is no longer reachable, and is vendored here unmodified so that this tree builds without the dead remote. The LGPL v3 permits this use inside a GPL v3 work. If you modify those files, they stay under the LGPL and the modifications have to be marked as such.
