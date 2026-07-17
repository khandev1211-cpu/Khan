# OCR (Tesseract bridge)

Real text recognition, via a native bridge to libtesseract — not a mock,
not a CLI shell-out. `src/ocr_lib.c` links against libtesseract's C API
(`tesseract/capi.h`) directly and feeds it pixel data already decoded by
`vision_load()`, so anything `vision.kh` can open (PNG, JPEG, BMP, GIF,
TGA, ...) works here too, with no temp files in between.

For the Khan-level API (`read_text()`, `read_text_with_confidence()`,
the lower-level `ocr_init`/`ocr_recognize`/`ocr_confidence`/
`ocr_set_page_seg_mode`/`ocr_free` natives), see the header comment in
`packages/ocr/ocr.kh` — this doc covers getting it to build and run.

## This is a real build-time dependency

Unlike most of Khan's packages, OCR needs something installed on the
machine you *build* Khan on. If libtesseract isn't found, `make` fails
outright — on purpose. Earlier versions of the `vision` package shipped
fakes for things that weren't really implemented (see `vision_lib.h`);
this package deliberately doesn't repeat that.

### Linux (Debian/Ubuntu)

```bash
sudo apt-get install libtesseract-dev libtesseract5 tesseract-ocr-eng
```

`pkg-config` (already on most systems) finds the right `-I`/`-L` flags
automatically — the makefile calls it for you. Add more languages with
`tesseract-ocr-<code>` (e.g. `tesseract-ocr-fra`, `tesseract-ocr-deu`).

### macOS (Homebrew)

```bash
brew install tesseract
```

The makefile's `pkg-config` call handles Homebrew's install prefix
either way (`/opt/homebrew` on Apple Silicon, `/usr/local` on Intel) —
this is the one case a bare `-ltesseract` would likely miss, which is
why the makefile doesn't just hardcode the flag.

### Windows

No system-wide `pkg-config` is assumed on a bare MinGW toolchain, so the
makefile links `-ltesseract` directly. The simplest path to a
gcc-linkable tesseract is MSYS2:

```bash
pacman -S mingw-w64-x86_64-tesseract-ocr
```

which installs into the toolchain's own prefix (already on the default
include/lib search path — no manual `-I`/`-L` needed). **This path is
unverified** — it hasn't been run through an actual Windows build in
this project's CI. If you hit include/link errors on Windows, that's
the first thing to check.

## Runtime: trained language data

Tesseract needs `<lang>.traineddata` files (installed alongside the
library via `tesseract-ocr-eng` / the Homebrew formula / the MSYS2
package above). When `ocr_init()` isn't given an explicit datapath, it
now searches a short list of common install locations itself
(`/usr/share/tesseract-ocr/*/tessdata` on Linux, the Homebrew prefix on
macOS, `C:/msys64/mingw64/share/tessdata` and `C:/Program
Files/Tesseract-OCR/tessdata` on Windows) before falling through to
Tesseract's own default handling.

This exists because Tesseract's own behavior with a `NULL` datapath
turned out not to be consistent across packagers: on Ubuntu's apt
build it correctly falls back to the real install path on its own; on
MSYS2's mingw-w64 build (confirmed against a real build) it just tries
`./` relative to the current directory and gives up. Rather than trust
that to be uniform everywhere, `ocr_init()` checks the likely locations
itself first.

If `ocr_init("eng")` still returns `nil` and stderr says it couldn't
find `eng.traineddata`, either:

- the language pack genuinely isn't installed, or
- your install puts `tessdata/` somewhere none of the built-in
  candidates check (a non-default MSYS2 install drive, for example) —
  either set the `TESSDATA_PREFIX` environment variable (checked
  first, before any guessing), or pass the directory explicitly:
  `ocr_init("eng", "/path/to/tessdata")`.

## Tuning recognition

- `vision_grayscale(image)` before `ocr_recognize()` generally improves
  accuracy — Tesseract does its own internal thresholding, but starting
  from grayscale gives it less to fight.
- `ocr_set_page_seg_mode(engine, mode)` changes how Tesseract segments
  the page (0-13, Tesseract's standard `PSM_*` numbering — same numbers
  as the `tesseract --psm` CLI flag). The most useful ones in practice:
  `3` (default — fully automatic layout), `6` (assume one uniform block
  of text), `7` (assume a single line — good for a cropped field like a
  serial number or ID), `11` (sparse text — scattered words with no
  consistent layout).
- Recognizing many images in a loop? Reuse one `ocr_init()` engine
  across all of them rather than calling `read_text()` per image —
  `ocr_init()` reloads the language model from disk every time it's
  called. See `packages/ocr/ocr.kh`'s header comment for the pattern.

## Tests

`tests/test_ocr.kh` is a real, automated suite (uses the checked-in
`examples/ocr_test.png` — rendered text, not a screenshot, so the
expected output is deterministic) — not a manual-inspection demo like
`examples/vision_test_all.kh`. Run it the same way as the others:

```bash
./khan tests/test_ocr.kh
```
