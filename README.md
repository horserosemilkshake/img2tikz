# img2tikz

Convert a JPEG/PNG/WEBP/SVG image into TikZ drawing commands.

The converter rasterizes the input image and emits a run-length-compressed set
of colored rectangles (`\path ... rectangle ...`) inside a standalone LaTeX
document containing a `tikzpicture`.

## Build

```sh
make
```

This builds `./img2tikz`.

## Build With CMake

```sh
cmake -S . -B build
cmake --build build
```

This also builds `img2tikz` using a cross-platform CMake workflow.

## Usage

```sh
./img2tikz [--cell <pt>] [--max-side <px>] <input_image> [output.tex]
```

Arguments:

- `--cell <pt>`: TikZ size in points for one source pixel (default `1.0`).
- `--max-side <px>`: Downscale so the longest side is at most this many pixels
  before conversion (default `256`, use `0` to disable downscaling).
- `<input_image>`: Path to `.jpg`, `.jpeg`, `.png`, `.webp`, or `.svg`.
- `[output.tex]`: Output file path; if omitted, writes TikZ to stdout.

Examples:

```sh
./img2tikz photo.jpg photo.tex
./img2tikz --max-side 512 --cell 0.6 logo.png logo.tex
./img2tikz icon.svg > icon.tex
```

## Compile Output

Generated `.tex` files are directly compilable with `pdflatex` (no
`standalone` class required).

```sh
pdflatex your-image.tex
```

If you want to embed the picture in an existing document instead, copy only the
`tikzpicture` environment from the generated file.

## Test The Executable

Use the batch script below to run an end-to-end test:

```sh
bash samples/generate_all.sh
```

What it does:

- Scans `samples/input` for supported image files (`.jpg`, `.jpeg`, `.png`, `.webp`, `.svg`).
- Converts each image to TikZ in `samples/tikz-output`.
- Compiles generated `.tex` files to PDFs in `samples/pdf-output`.

Useful overrides:

```sh
MAX_SIDE=1920 QUANT_STEP=16 bash samples/generate_all.sh
```

Run unit tests:

```sh
make tests
```

Run unit tests with CMake/CTest:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

- `MAX_SIDE`: Maximum raster side length before conversion.
- `QUANT_STEP`: Color quantization step (higher values reduce TikZ complexity).
- `MIN_SIDE`: Lower bound used by the script when it retries after LaTeX compile failures.
- `CELL_PT`: TikZ point size per source pixel.

## Notes

- Large images generate very large TikZ files. `--max-side` helps keep output manageable.
- Transparent pixels are composited over white before emitting TikZ colors.
- WebP decoding uses `ffmpeg` at runtime; ensure `ffmpeg` is available in `PATH`.