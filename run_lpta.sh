#!/bin/bash
# LPTA - Build and Run Script
# Usage: bash run_lpta.sh [input.ll] [--snapshots]

LLVM_DIR="C:/LLVM-full/clang+llvm-22.1.8-x86_64-pc-windows-msvc"
BUILD_DIR="C:/LLVM-full/build"
REPORT_DIR="C:/LLVM-full/report"
INPUT="${1:-C:/LLVM-full/test.ll}"
SNAPSHOTS="${2:-}"

export PATH="/c/Program Files/CMake/bin:/c/Users/ramri/AppData/Local/Microsoft/WinGet/Links:$PATH"

echo "=== LPTA Build & Run ==="
echo ""

# Ensure build directory exists
if [ ! -d "$BUILD_DIR" ]; then
  mkdir -p "$BUILD_DIR"
fi

# Build
echo "[1/3] Building..."
cd "$BUILD_DIR" || { echo "ERROR: cannot cd to $BUILD_DIR"; exit 1; }

cmake -G Ninja \
  -DCMAKE_CXX_COMPILER="$LLVM_DIR/bin/clang-cl.exe" \
  -DCMAKE_LINKER="$LLVM_DIR/bin/lld-link.exe" \
  -DCMAKE_MAKE_PROGRAM="C:/Users/ramri/AppData/Local/Microsoft/WinGet/Packages/Ninja-build.Ninja_Microsoft.Winget.Source_8wekyb3d8bbwe/ninja.exe" \
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
cp "C:/LLVM-full/dashboard.html" "$REPORT_DIR/index.html"

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
