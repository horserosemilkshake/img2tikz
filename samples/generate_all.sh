#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

INPUT_DIR="$REPO_ROOT/samples/input"
TIKZ_DIR="$REPO_ROOT/samples/tikz-output"
PDF_DIR="$REPO_ROOT/samples/pdf-output"

IMG2TIKZ_BIN="${IMG2TIKZ_BIN:-$REPO_ROOT/img2tikz}"
CELL_PT="${CELL_PT:-0.8}"
MAX_SIDE="${MAX_SIDE:-1920}"
QUANT_STEP="${QUANT_STEP:-16}"
MIN_SIDE="${MIN_SIDE:-128}"

mkdir -p "$INPUT_DIR" "$TIKZ_DIR" "$PDF_DIR"

if [[ ! -x "$IMG2TIKZ_BIN" ]]; then
    echo "img2tikz binary not found at '$IMG2TIKZ_BIN'. Building it..."
    make -C "$REPO_ROOT"
fi

if ! command -v pdflatex >/dev/null 2>&1; then
    echo "pdflatex is required but not found in PATH." >&2
    exit 1
fi

images=()
while IFS= read -r -d '' img; do
    images+=("$img")
done < <(
    find "$INPUT_DIR" -maxdepth 1 -type f \
        \( -iname "*.jpg" -o -iname "*.jpeg" -o -iname "*.png" -o -iname "*.webp" -o -iname "*.svg" \) \
        -print0 | sort -z
)

if [[ ${#images[@]} -eq 0 ]]; then
    echo "No supported input images found in '$INPUT_DIR'." >&2
    exit 1
fi

echo "Converting ${#images[@]} input image(s) to TikZ and compiling to PDF..."
for img in "${images[@]}"; do
    base="$(basename "$img")"
    stem="${base%.*}"
    ext="${base##*.}"
    ext="${ext,,}"
    tex_out="$TIKZ_DIR/${stem}-${ext}.tex"

    side="$MAX_SIDE"
    compiled=0
    while [[ "$side" -ge "$MIN_SIDE" ]]; do
        "$IMG2TIKZ_BIN" --max-side "$side" --cell "$CELL_PT" --quant-step "$QUANT_STEP" "$img" "$tex_out"
        if pdflatex -interaction=nonstopmode -halt-on-error -output-directory "$PDF_DIR" "$tex_out" >/dev/null 2>&1; then
            echo "  [OK] $base -> $(basename "$tex_out") -> ${stem}-${ext}.pdf (max-side=$side, quant-step=$QUANT_STEP)"
            compiled=1
            break
        fi

        next_side=$((side * 3 / 4))
        if [[ "$next_side" -ge "$side" ]]; then
            next_side=$((side - 1))
        fi
        if [[ "$next_side" -lt "$MIN_SIDE" ]]; then
            break
        fi
        side="$next_side"
    done

    if [[ "$compiled" -ne 1 ]]; then
        log_file="$PDF_DIR/${stem}-${ext}.log"
        echo "  [FAIL] $base"
        echo "  pdflatex log: $log_file"
        echo "  Hint: increase QUANT_STEP or lower MIN_SIDE."
        exit 1
    fi
done

echo "Done. Outputs are in:"
echo "  $TIKZ_DIR"
echo "  $PDF_DIR"
