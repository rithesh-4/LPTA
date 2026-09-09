#!/bin/bash
# LPTA - Build and Run Script
# Usage: bash run_lpta.sh [input.ll|input.c] [report_dir] [-O0|-O1|-O2|-O3|-Os|-Oz] [--snapshots] [--targets=...] [--]
#
# Environment variables:
#   LLVM_DIR    - Path to LLVM installation (REQUIRED if not auto-detectable)
#   BUILD_DIR   - Build directory (default: ./build)
#   REPORT_DIR  - Output directory (default: ./report)
#   CLANG       - C/C++ compiler for .c inputs (default: bundled clang, else PATH)
#   LPTA_CFLAGS - Extra flags for the .c -> IR step (default: -O0).
#                 E.g. target/headers for bare-metal code:
#                 LPTA_CFLAGS="--target=arm-none-eabi -mcpu=cortex-m4 -I PATH -DSTM32F407xx"

set -euo pipefail

usage() {
    cat <<'EOF'
Usage: bash run_lpta.sh [input.ll|input.c] [report_dir] [-O0|-O1|-O2|-O3|-Os|-Oz] [--snapshots] [--targets=...] [--]

  input.ll/c      LLVM IR (.ll) analyzed directly; C/C++ (.c/.cpp/...) is
                  first compiled to IR with clang (see CLANG, LPTA_CFLAGS).
                  Default input: ./test.ll
  report_dir      Output directory (default: ./report or $REPORT_DIR)
  -O0..-Oz        Optimization level (default: -O2, last one wins)
  --snapshots     Save per-pass IR snapshots
  --no-ir-hash    Perf mode: skip IR hashing (ir_changed stays false)
  --targets=...   common | triple,... | @file
  --              End of flags (following args are paths)
  --version       Build, then print the tool + LLVM version

Environment:
  LLVM_DIR        LLVM install prefix (auto-detected via llvm-config or bundled dir)
  BUILD_DIR       Build directory (default: ./build)
  REPORT_DIR      Default output directory (default: ./report)
EOF
}

# Resolve the directory containing this script (robust to `bash path/to/run_lpta.sh`
# invocations and paths with spaces). Must run before any `cd`.
SCRIPT_SRC="${BASH_SOURCE[0]:-$0}"
SCRIPT_DIR="$(cd "$(dirname "$SCRIPT_SRC")" && pwd)"

# Auto-detect LLVM: explicit $LLVM_DIR wins, otherwise llvm-config, then the
# bundled clang+llvm-*/ dir next to this script, then common install prefixes.
LLVM_DIR="${LLVM_DIR:-}"
if [ -z "$LLVM_DIR" ]; then
    if command -v llvm-config >/dev/null 2>&1; then
        LLVM_DIR="$(llvm-config --prefix 2>/dev/null || true)"
    fi
fi
if [ -z "$LLVM_DIR" ]; then
    for cand in "$SCRIPT_DIR"/clang+llvm-*/; do
        if [ -d "$cand" ]; then
            LLVM_DIR="$cand"
            break
        fi
    done
fi

# Normalize: accept either the install prefix (<prefix>/lib/cmake/llvm exists)
# or the CMake config dir itself (<prefix>/lib/cmake/llvm passed directly).
LLVM_CMAKE_DIR=""
if [ -n "$LLVM_DIR" ] && [ -d "$LLVM_DIR/lib/cmake/llvm" ]; then
    LLVM_CMAKE_DIR="$LLVM_DIR/lib/cmake/llvm"
elif [ -n "$LLVM_DIR" ] && [ -f "$LLVM_DIR/LLVMConfig.cmake" ]; then
    LLVM_CMAKE_DIR="$LLVM_DIR"
fi

if [ -z "$LLVM_CMAKE_DIR" ]; then
    echo "ERROR: LLVM not found." >&2
    echo "" >&2
    echo "Set LLVM_DIR to your LLVM installation:" >&2
    echo "  export LLVM_DIR=/path/to/llvm" >&2
    echo "" >&2
    echo "Example:" >&2
    echo "  export LLVM_DIR=/usr/local/llvm-22" >&2
    echo "  bash run_lpta.sh input.ll" >&2
    if [ -n "$LLVM_DIR" ]; then
        echo "" >&2
        echo "  (LLVM_DIR is currently set to '$LLVM_DIR'," >&2
        echo "   but '$LLVM_DIR/lib/cmake/llvm' was not found.)" >&2
    fi
    exit 1
fi
# Strip trailing slash for tidy log output.
LLVM_DIR="${LLVM_DIR%/}"

BUILD_DIR="${BUILD_DIR:-./build}"
REPORT_DIR_DEFAULT="${REPORT_DIR:-./report}"
INPUT=""
REPORT_POS=""
OPT_LEVEL=""
SNAPSHOTS=""
NO_IR_HASH=""
TARGETS=""
SHOW_VERSION=""
END_OF_FLAGS=0
add_positional() {
  if [ -z "$INPUT" ]; then
    INPUT="$1"
  elif [ -z "$REPORT_POS" ]; then
    REPORT_POS="$1"
  else
    echo "ERROR: unexpected argument '$1'" >&2
    echo "" >&2
    usage >&2
    exit 1
  fi
}
for arg in "$@"; do
  if [ "$END_OF_FLAGS" -eq 0 ] && [ "$arg" = "--" ]; then
    END_OF_FLAGS=1
    continue
  fi
  if [ "$END_OF_FLAGS" -eq 1 ]; then
    add_positional "$arg"
    continue
  fi
  case "$arg" in
    -h|--help) usage; exit 0 ;;
    --version|-version) SHOW_VERSION=1 ;;
    --snapshots) SNAPSHOTS="--snapshots" ;;
    --no-ir-hash) NO_IR_HASH="--no-ir-hash" ;;
    --targets=*) TARGETS="$arg" ;;
    -O0|-O1|-O2|-O3|-Os|-Oz) OPT_LEVEL="$arg" ;;
    -*) echo "ERROR: unknown flag '$arg'" >&2; echo "" >&2; usage >&2; exit 1 ;;
    *) add_positional "$arg" ;;
  esac
done
INPUT="${INPUT:-./test.ll}"
REPORT_DIR="${REPORT_POS:-$REPORT_DIR_DEFAULT}"

# Resolve to absolute paths BEFORE cd'ing into BUILD_DIR (relative
# inputs like test.ll would otherwise resolve inside build/).
ROOT="$(pwd)"
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

if [ -z "$SHOW_VERSION" ] && [ ! -f "$INPUT" ]; then
    echo "ERROR: input file not found: '$INPUT'" >&2
    exit 1
fi
if [ ! -f "$SCRIPT_DIR/dashboard.html" ]; then
    echo "ERROR: dashboard.html not found next to run_lpta.sh: '$SCRIPT_DIR/dashboard.html'" >&2
    exit 1
fi

# C/C++ passthrough: compile to LLVM IR first, then analyze the IR.
# Keeps INPUT as the user's file for messages; GEN_LL is what LPTA runs on.
GEN_LL=""
case "$INPUT" in
    *.c|*.C|*.cpp|*.cc|*.cxx|*.c++|*.C++)
        # Locate clang: $CLANG wins, then the bundled compiler, then PATH.
        CLANG="${CLANG:-}"
        if [ -z "$CLANG" ]; then
            # Unmatched globs stay literal and fail the -x test, so this
            # is safe even with zero or many bundled toolchains.
            for cand in "$SCRIPT_DIR"/clang+llvm-*/bin/clang.exe \
                        "$SCRIPT_DIR"/clang+llvm-*/bin/clang; do
                if [ -x "$cand" ]; then CLANG="$cand"; break; fi
            done
        fi
        if [ -z "$CLANG" ] && command -v clang >/dev/null 2>&1; then
            CLANG="clang"
        fi
        if [ -z "$CLANG" ]; then
            echo "ERROR: C/C++ input needs clang, but none was found." >&2
            echo "  Set CLANG=/path/to/clang or place a bundled clang+llvm-*/ next to run_lpta.sh." >&2
            exit 1
        fi
        SRC_BASE="$(basename "$INPUT")"
        SRC_BASE="${SRC_BASE%.*}"
        # Sanitize to a safe stem (spaces etc. would leak into history.json names)
        SRC_BASE="$(printf '%s' "$SRC_BASE" | tr -c 'A-Za-z0-9_.-' '_')"
        GEN_LL="$BUILD_DIR/lpta_input_${SRC_BASE}.ll"
        mkdir -p "$BUILD_DIR"
        echo "  Compiling $INPUT -> $GEN_LL"
        echo "    clang flags: ${LPTA_CFLAGS:--O0 (default; override with LPTA_CFLAGS)}"
        # shellcheck disable=SC2086: LPTA_CFLAGS is intentionally word-split
        if ! "$CLANG" -S -emit-llvm ${LPTA_CFLAGS:--O0} "$INPUT" -o "$GEN_LL" 2>"$BUILD_DIR/clang.log"; then
            echo "ERROR: clang failed on '$INPUT'. Last 30 lines of $BUILD_DIR/clang.log:" >&2
            tail -30 "$BUILD_DIR/clang.log" >&2 || true
            exit 1
        fi
        INPUT="$GEN_LL"
        ;;
esac
for tool in cmake ninja; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "ERROR: required tool '$tool' not found on PATH." >&2
        exit 1
    fi
done

echo "=== LPTA Build & Run ==="
echo "  LLVM:       $LLVM_DIR"
echo "  Input:      $INPUT"
echo "  Report dir: $REPORT_DIR"
echo "  Build dir:  $BUILD_DIR"
echo "  Opt level:  ${OPT_LEVEL:-(default -O2)}"
echo ""

# Ensure build directory exists
mkdir -p "$BUILD_DIR"

# Build (logs kept so failures are diagnosable instead of swallowed)
echo "[1/3] Building..."
if ! cmake -G Ninja -DLLVM_DIR="$LLVM_CMAKE_DIR" -S "$SCRIPT_DIR" -B "$BUILD_DIR" >"$BUILD_DIR/cmake.log" 2>&1; then
    echo "ERROR: CMake configuration failed. Last 30 lines of $BUILD_DIR/cmake.log:" >&2
    tail -30 "$BUILD_DIR/cmake.log" >&2 || true
    exit 1
fi
if ! ninja -C "$BUILD_DIR" >"$BUILD_DIR/ninja.log" 2>&1; then
    echo "ERROR: Build failed. Last 30 lines of $BUILD_DIR/ninja.log:" >&2
    tail -30 "$BUILD_DIR/ninja.log" >&2 || true
    exit 1
fi
echo "  Build OK"

# Locate the tool binary (`.exe` on Windows, bare name elsewhere/MSYS).
LPTA_BIN=""
for cand in "$BUILD_DIR/lpta_test.exe" "$BUILD_DIR/lpta_test"; do
    if [ -x "$cand" ]; then
        LPTA_BIN="$cand"
        break
    fi
done
if [ -z "$LPTA_BIN" ]; then
    echo "ERROR: lpta_test binary not found after build (looked in $BUILD_DIR)." >&2
    echo "See $BUILD_DIR/ninja.log for details." >&2
    exit 1
fi

# Passthrough query: build, then report the tool version.
if [ -n "$SHOW_VERSION" ]; then
    "$LPTA_BIN" --version
    exit $?
fi

# Run (capture full output; show a filtered summary but never hide failures).
echo "[2/3] Running LPTA on $INPUT..."
mkdir -p "$REPORT_DIR"
RUN_ARGS=("$INPUT" "$REPORT_DIR")
[ -n "$OPT_LEVEL" ] && RUN_ARGS+=("$OPT_LEVEL")
[ -n "$SNAPSHOTS" ] && RUN_ARGS+=("$SNAPSHOTS")
[ -n "$NO_IR_HASH" ] && RUN_ARGS+=("$NO_IR_HASH")
[ -n "$TARGETS" ] && RUN_ARGS+=("$TARGETS")
RUN_LOG="$REPORT_DIR/lpta_run.log"
set +e
"$LPTA_BIN" "${RUN_ARGS[@]}" >"$RUN_LOG" 2>&1
LPTA_RC=$?
set -e
grep -E "^(===|\[|  |Wrote|Module:|Pipeline:|Output:|  Using)" "$RUN_LOG" | head -40 || true
echo ""
if [ $LPTA_RC -ne 0 ]; then
    echo "ERROR: lpta_test failed with exit code $LPTA_RC. Last 30 lines of $RUN_LOG:" >&2
    tail -30 "$RUN_LOG" >&2 || true
    exit "$LPTA_RC"
fi
if [ ! -f "$REPORT_DIR/history.json" ]; then
    echo "ERROR: lpta_test succeeded but '$REPORT_DIR/history.json' was not created." >&2
    echo "See $RUN_LOG for details." >&2
    exit 1
fi

# Copy dashboard
echo "[3/3] Dashboard ready at: $REPORT_DIR/index.html"
if ! cp "$SCRIPT_DIR/dashboard.html" "$REPORT_DIR/index.html"; then
    echo "ERROR: failed to copy dashboard to '$REPORT_DIR/index.html'." >&2
    exit 1
fi

echo ""
echo "=== Done ==="
echo "  JSON:   $REPORT_DIR/history.json"
echo "  HTML:   $REPORT_DIR/index.html"
echo ""
echo "  Open dashboard:"
echo "    python \"$SCRIPT_DIR/serve_dashboard.py\" \"$REPORT_DIR\" -p 8080"
echo "    Then open http://localhost:8080"
echo ""
