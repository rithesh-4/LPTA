#!/bin/bash
# LPTA - Build and Run Script
# Usage: bash run_lpta.sh [input.ll] [--snapshots]
#
# Environment variables:
#   LLVM_DIR   - Path to LLVM installation (default: C:/LLVM-full/clang+llvm-22.1.8-x86_64-pc-windows-msvc)
#   BUILD_DIR  - Build directory (default: ./build)
#   REPORT_DIR - Output directory (default: ./report)

LLVM_DIR="${LLVM_DIR:-C:/LLVM-full/clang+llvm-22.1.8-x86_64-pc-windows-msvc}"
BUILD_DIR="${BUILD_DIR:-./build}"
REPORT_DIR="${REPORT_DIR:-./report}"
INPUT="${1:-C:/LLVM-full/test.ll}"
SNAPSHOTS="${2:-}"

export PATH="/c/Program Files/CMake/bin:/c/Users/ramri/AppData/Local/Microsoft/WinGet/Links:$PATH"

echo "=== LPTA Build & Run ==="
echo "  LLVM: $LLVM_DIR"
echo ""

# Ensure build directory exists
if [ ! -d "$BUILD_DIR" ]; then
  mkdir -p "$BUILD_DIR"
fi

# Build
echo "[1/3] Building..."
cd "$BUILD_DIR" || { echo "ERROR: cannot cd to $BUILD_DIR"; exit 1; }

cmake -G Ninja \
  -DLLVM_DIR="$LLVM_DIR/lib/cmake/llvm" \
  -DCMAKE_CXX_COMPILER="$LLVM_DIR/bin/clang-cl.exe" \
  -DCMAKE_LINKER="$LLVM_DIR/bin/lld-link.exe" \
  .. > /dev/null 2>&1

ninja > /dev/null 2>&1
echo "  Build OK"

# Run
echo "[2/3] Running LPTA on $INPUT..."
mkdir -p "$REPORT_DIR"
"$BUILD_DIR/lpta_test.exe" "$INPUT" "$REPORT_DIR" $SNAPSHOTS 2>&1 | grep -E "^(===|\[|  |Wrote|Module:|Pipeline:|Output:)" | head -40
echo ""

# Copy dashboard
echo "[3/3] Dashboard ready at: $REPORT_DIR/index.html"
cp "$(dirname "$0")/dashboard.html" "$REPORT_DIR/index.html"

echo ""
echo "=== Done ==="
echo "  JSON:   $REPORT_DIR/history.json"
echo "  HTML:   $REPORT_DIR/index.html"
echo ""
echo "  Open dashboard:"
echo "    cd $REPORT_DIR"
echo "    python -m http.server 8080"
echo "    Then open http://localhost:8080"
echo ""
