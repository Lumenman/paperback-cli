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

`--dpi` is the density of code dots (40–600, default 150). `--image-dpi` is the bitmap resolution (80–2400), defaulting to three times the dot density. It must be at least twice the requested dot density. Dot spacing is rounded to whole pixels; the CLI reports the resulting density. `--dotsize` is the dot width as a percentage of spacing (50–100, default 70), also rounded to pixels -- and at a small cell that rounding leaves it very few steps, so the CLI reports the cell and the dot it actually drew, in pixels and in percent. With `--image-dpi` left at its default the cell is 3 pixels, where `-s 55` and `-s 70` draw the same 2-pixel dot and `-s 85` and `-s 100` draw the same 3-pixel one: printing sheets at 55, 70 and 85 to compare them yields two settings, not three. Raising `--image-dpi` to the printer's own resolution buys both finer steps and one less resampling on the way to paper. Large page/resolution combinations exceeding 32768 pixels per side or approximately 256 MiB of sheet pixels are rejected.

`--redundancy N` adds one recovery block per N data blocks (2–10, default 5). **Smaller N provides more redundancy** and less capacity. `--border` enables the outer raster border; it only helps scanners with clumsy auto-cropping and does nothing for recognition, so it is off by default. `--header` prints a text band above and below the grid: file name, modification time, size and page number on top, the SHA-256 of the input file and the recommended scan resolution at the bottom. The bands take space from the grid, costing 3 to 5 % of the capacity depending on dot density, so they are off by default too. `--encode` prints the same SHA-256 to standard output whether or not the header is on, so you can record it separately. Both header lines use the same glyph size, chosen so the longer of the two fits the sheet. Create archives or encrypted files using external tools before encoding. The CLI treats them as ordinary bytes and never unpacks or decrypts them.

## Restore from one or more scans

```sh
paperback-cli --decode -i scan.bmp -o restored.bin
paperback-cli --decode -o restored.bin scan1.bmp scan2.bmp scan3.bmp
paperback-cli --decode -i scan1.bmp -i scan2.bmp -o restored.bin
paperback-cli --decode -i scan.bmp -p 4 -o restored.bin
```

The last command reads `scan_0001.bmp` through `scan_0004.bmp`. A numbered sequence accepts one input basename. Other forms accept arbitrary scan paths. Input is uncompressed 8-bit paletted or 24-bit BMP, including top-down BMPs; convert PNG/JPEG/TIFF scans first.

Scans may be repeated, supplied out of order, or rotated by 180 degrees. Rotation by multiples of 90 degrees and mirroring are detected by the decoder; small skew is also supported. Successfully decoded blocks and recovery blocks accumulate across all scans in a single invocation. Repeated scans can fill each other's gaps. This combines decoded blocks, not image pixels. Scans identified as different files cannot be written to the same output.

`--quality-map` prints one character per block of every page decoded: a digit for a block that was read (the digit counts the bytes its ECC had to repair), `+` for ten or more repairs, `#` for a block that was located but stayed unreadable, and `.` for a block the grid search never found. The pattern names the cause. Dots in a contiguous band or at the edges mean the raster itself was lost there: the sheet is warped, cropped or skewed. Hashes scattered over the page mean the raster was found but the dots are too poor to read: print the page at the printer's native resolution with `--image-dpi`, scan at four pixels per dot, and turn off the scanner's auto-levels, sharpening and descreening. Rising digits across an otherwise clean page mean the margin is thinning before it runs out.

Every page carries the name of the file it holds, and the decoder prints it as `Page label:` once per file, whether or not `-o` was given. A sheet of unknown provenance therefore says what it is before it is restored, which is the one thing `-o` cannot tell you. Control characters in it are replaced with `?`, because the bytes come off a scanned sheet and an escape sequence would be as easy to print there as a letter.

A stack of sheets may hold several files at once, up to five: each carries its own name, so they do not have to be sorted by hand and fed in one run at a time. Every file that can be written is written, and the exit status reports the worst outcome among them, so one unreadable file does not cost you the rest. `-o` and `--expect` cannot be used then -- one names a single path, the other a single digest -- and the decoder says so rather than picking a file for you.

`-o` is optional when decoding. Without it the file is restored in the current directory under that same name, which also restores its extension, so the type no longer has to be guessed at. The label is read as a name and never as a path: everything up to the last `/`, `\` or `:` is dropped, so a label of `../../etc/passwd` restores as `passwd`; characters a name may not hold are replaced with `_`; a label that leaves nothing usable, or that names a Windows device, is refused and asks for `-o`. A name taken off a sheet never overwrites an existing file -- the decode fails and says so instead. With `-o` the path is used exactly and overwrites, as before: the caller named it.

A sheet is measured over its whole inked area rather than a window in the middle of it, so a sticker, a coffee ring or a white crease does not decide the fate of the page it landed on: the blocks under it are lost and the rest is read. Only the raster search still works from a band across the centre, so a blank stripe four centimetres wide running the full height of the sheet can still cost the page.

Every scan is also measured against the page's own ruler. The dot is printed a known fraction of the cell -- `--dotsize`, rounded by the printer to whole pixels -- and ink on paper only ever spreads: through a printer, a sheet of paper and a pane of glass a dot comes back wider, never narrower. So a dot that measures narrower than it was printed says something in the scan squared off its edges, which is auto-levels, sharpening, the scanner's own contrast or a white point. That damage is done before the file is written, it leaves no other trace in the image, and it throws away exactly the pixels of partial coverage the decoder reads. The decoder says so and names the fix; rescan with every adjustment off. Pass the `-s` the page was printed with if it was not the default, or the ruler is measured against the wrong number. `--quality-map` also prints the measurement itself, whatever it came to.

The checksum the page carries for the whole file is 16 bits, and so is the one each block carries for itself. Both are there to catch damage, and they do; neither is proof. A block whose Reed-Solomon correction had to be pushed to its limit rests on those 16 bits alone, and one such block in 65536 is accepted wrongly -- which the file checksum then catches, itself 16 bits. For a backup that matters, record the SHA-256 the encoder prints and check the restore against it with `--expect`; `--header` prints it on the sheet as well.

Every restore prints the SHA-256 of what it wrote, so it can be checked by eye against the digest the encoder printed or `--header` put on the sheet. A partial restore says so beside its digest: its gaps are zeros, so it cannot match the sheet and is only good for telling two attempts at the same damaged page apart.

`--expect HEX` checks the restored bytes against a SHA-256 digest, the one printed at encode time or read off the page footer. Case does not matter. On a mismatch the file is still written, the two digests are reported and the exit status is 1, so a wrong or mixed-up backup cannot pass silently.

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
