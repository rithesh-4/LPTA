#!/bin/bash
# LPTA - Build and Run Script
# Usage: bash run_lpta.sh [input.ll] [report_dir] [-O0|-O1|-O2|-O3|-Os|-Oz] [--snapshots] [--targets=...]
#
# Environment variables:
#   LLVM_DIR   - Path to LLVM installation (REQUIRED if not auto-detectable)
#   BUILD_DIR  - Build directory (default: ./build)
#   REPORT_DIR - Output directory (default: ./report)

set -e

# Auto-detect: check if LLVM_DIR is set, otherwise try common locations
if [ -z "$LLVM_DIR" ]; then
    # Try to find LLVM via llvm-config
    if command -v llvm-config &>/dev/null; then
        LLVM_DIR="$(llvm-config --prefix 2>/dev/null || true)"
    fi
fi

if [ -z "$LLVM_DIR" ] || [ ! -d "$LLVM_DIR/lib/cmake/llvm" ]; then
    echo "ERROR: LLVM not found."
    echo ""
    echo "Set LLVM_DIR to your LLVM installation:"
    echo "  export LLVM_DIR=/path/to/llvm"
    echo ""
    echo "Example:"
    echo "  export LLVM_DIR=/usr/local/llvm-22"
    echo "  bash run_lpta.sh input.ll"
    exit 1
fi

BUILD_DIR="${BUILD_DIR:-./build}"
REPORT_DIR_DEFAULT="${REPORT_DIR:-./report}"
INPUT=""
REPORT_POS=""
OPT_LEVEL=""
SNAPSHOTS=""
TARGETS=""
for arg in "$@"; do
  case "$arg" in
    --snapshots) SNAPSHOTS="--snapshots" ;;
    --targets=*) TARGETS="$arg" ;;
    -O0|-O1|-O2|-O3|-Os|-Oz) OPT_LEVEL="$arg" ;;
    -*) echo "ERROR: unknown flag '$arg'"; exit 1 ;;
    *)
      if [ -z "$INPUT" ]; then
        INPUT="$arg"
      elif [ -z "$REPORT_POS" ]; then
        REPORT_POS="$arg"
      else
        echo "ERROR: unexpected argument '$arg'"
        exit 1
      fi
      ;;
  esac
done
INPUT="${INPUT:-./test.ll}"
REPORT_DIR="${REPORT_POS:-$REPORT_DIR_DEFAULT}"

# Resolve to absolute paths BEFORE cd'ing into BUILD_DIR (relative
# inputs like test.ll would otherwise resolve inside build/).
# SCRIPT_DIR must also be captured here — after cd, $0's directory
# would resolve inside build/ and the dashboard copy would fail.
ROOT="$(pwd)"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
case "$INPUT" in
  /*|[A-Za-z]:*) ;;
  *) INPUT="$ROOT/$INPUT" ;;
esac
case "$REPORT_DIR" in
  /*|[A-Za-z]:*) ;;
  *) REPORT_DIR="$ROOT/$REPORT_DIR" ;;
esac
case "$BUILD_DIR" in
  /*|[A-Za-z]:*) ;;
  *) BUILD_DIR="$ROOT/$BUILD_DIR" ;;
esac

echo "=== LPTA Build & Run ==="
echo "  LLVM: $LLVM_DIR"
echo ""

# Ensure build directory exists
mkdir -p "$BUILD_DIR"

# Build
echo "[1/3] Building..."
cd "$BUILD_DIR"

cmake -G Ninja \
  -DLLVM_DIR="$LLVM_DIR/lib/cmake/llvm" \
  .. > /dev/null 2>&1

ninja > /dev/null 2>&1
echo "  Build OK"

# Run
echo "[2/3] Running LPTA on $INPUT..."
mkdir -p "$REPORT_DIR"
./lpta_test "$INPUT" "$REPORT_DIR" $OPT_LEVEL $SNAPSHOTS $TARGETS 2>&1 | grep -E "^(===|\[|  |Wrote|Module:|Pipeline:|Output:|  Using)" | head -40
echo ""

# Copy dashboard
echo "[3/3] Dashboard ready at: $REPORT_DIR/index.html"
cp "$SCRIPT_DIR/dashboard.html" "$REPORT_DIR/index.html"

echo ""
echo "=== Done ==="
echo "  JSON:   $REPORT_DIR/history.json"
echo "  HTML:   $REPORT_DIR/index.html"
echo ""
echo "  Open dashboard:"
echo "    python \"$SCRIPT_DIR/serve_dashboard.py\" \"$REPORT_DIR\" -p 8080"
echo "    Then open http://localhost:8080"
echo ""
