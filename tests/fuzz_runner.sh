#!/bin/bash
# ============================================================
# LPTA Black-Box Fuzzer
#
# Mutates a seed IR file and feeds variants to lpta_test.
# Detects: crashes (non-zero exit), hangs (>30s timeout),
#          assertion failures, and unexpected output.
# ============================================================

set -u

EXE="${1:-./build/lpta_test.exe}"
SEED="${2:-test.ll}"
ITERATIONS="${3:-200}"
TIMEOUT=30
CRASH_DIR="tests/crashes"
PASS_DIR="tests/fuzz_pass"
LOG="tests/fuzz_results.log"

mkdir -p "$CRASH_DIR" "$PASS_DIR"

echo "=== LPTA Fuzz Test ===" > "$LOG"
echo "  Exe: $EXE" >> "$LOG"
echo "  Seed: $SEED" >> "$LOG"
echo "  Iterations: $ITERATIONS" >> "$LOG"
echo "  Timeout: ${TIMEOUT}s" >> "$LOG"
echo "" >> "$LOG"

if [ ! -f "$SEED" ]; then
    echo "ERROR: Seed file '$SEED' not found"
    exit 1
fi

SEED_SIZE=$(wc -c < "$SEED")
echo "  Seed size: $SEED_SIZE bytes" >> "$LOG"
echo "" >> "$LOG"

CRASHES=0
TIMEOUTS=0
PASS=0
SEED_CONTENT=$(cat "$SEED")

for i in $(seq 1 $ITERATIONS); do
    # Create mutation of the seed
    MUTANT="/tmp/fuzz_mutant_$i.ll"
    
    case $((RANDOM % 8)) in
        0)
            # Truncate at random point
            TRUNC=$((RANDOM % SEED_SIZE + 1))
            head -c $TRUNC "$SEED" > "$MUTANT"
            ;;
        1)
            # Insert random bytes at random position
            POS=$((RANDOM % SEED_SIZE + 1))
            head -c $POS "$SEED" > "$MUTANT"
            dd if=/dev/urandom bs=1 count=$((RANDOM % 32 + 1)) 2>/dev/null >> "$MUTANT"
            tail -c +$((POS + 1)) "$SEED" >> "$MUTANT"
            ;;
        2)
            # Delete random chunk
            START=$((RANDOM % SEED_SIZE))
            LEN=$((RANDOM % 50 + 1))
            head -c $START "$SEED" > "$MUTANT"
            tail -c +$((START + LEN + 1)) "$SEED" >> "$MUTANT"
            ;;
        3)
            # Flip random bytes
            cp "$SEED" "$MUTANT"
            FLIPS=$((RANDOM % 10 + 1))
            for f in $(seq 1 $FLIPS); do
                POS=$((RANDOM % SEED_SIZE + 1))
                BYTE=$(printf '\\x%02x' $((RANDOM % 256)))
                # Use dd to replace a byte at position
                BEFORE=$(head -c $((POS - 1)) "$MUTANT")
                AFTER=$(tail -c +$((POS + 1)) "$MUTANT")
                echo -n "$BEFORE" > "$MUTANT"
                echo -n "$BYTE" >> "$MUTANT" 2>/dev/null
                printf "\\$(printf '%03o' $((RANDOM % 256)))" >> "$MUTANT"
                echo -n "$AFTER" >> "$MUTANT"
            done
            ;;
        4)
            # Replace entire content with random bytes
            SIZE=$((RANDOM % 4096 + 1))
            dd if=/dev/urandom bs=1 count=$SIZE 2>/dev/null > "$MUTANT"
            ;;
        5)
            # Insert repeated "A" pattern (heap spray)
            POS=$((RANDOM % SEED_SIZE + 1))
            head -c $POS "$SEED" > "$MUTANT"
            printf 'A%.0s' $(seq 1 $((RANDOM % 1000 + 100))) >> "$MUTANT"
            tail -c +$((POS + 1)) "$SEED" >> "$MUTANT"
            ;;
        6)
            # Empty file
            : > "$MUTANT"
            ;;
        7)
            # Single null byte
            printf '\0' > "$MUTANT"
            ;;
    esac
    
    # Run with timeout
    OUTFILE="/tmp/fuzz_out_$i"
    mkdir -p "$OUTFILE"
    
    START_TIME=$(date +%s)
    # Vary flags so snapshot/cap, size-opt, and multi-target paths get
    # fuzzed too — not just the default -O2 run.
    case $((i % 10)) in
        0) FUZZ_FLAGS="--snapshots" ;;
        1) FUZZ_FLAGS="-Os" ;;
        2) FUZZ_FLAGS="--targets=common" ;;
        *) FUZZ_FLAGS="" ;;
    esac
    # shellcheck disable=SC2086: intentional word splitting of FUZZ_FLAGS
    timeout $TIMEOUT "$EXE" "$MUTANT" "$OUTFILE" -O2 $FUZZ_FLAGS >/dev/null 2>&1
    RC=$?
    END_TIME=$(date +%s)
    ELAPSED=$((END_TIME - START_TIME))
    
    if [ $RC -eq 124 ] || [ $ELAPSED -ge $TIMEOUT ]; then
        TIMEOUTS=$((TIMEOUTS + 1))
        echo "[TIMEOUT] Iteration $i (${ELAPSED}s)" >> "$LOG"
        cp "$MUTANT" "$CRASH_DIR/timeout_$i.ll"
    elif [ $RC -ne 0 ]; then
        # Non-zero exit — expected for bad inputs, but check if it's a crash
        OUTPUT=$(timeout 5 "$EXE" "$MUTANT" "$OUTFILE" -O2 2>&1)
        if echo "$OUTPUT" | grep -qiE "(segmentation|abort|signal|access violation|stack overflow|assertion)"; then
            CRASHES=$((CRASHES + 1))
            echo "[CRASH] Iteration $i (RC=$RC)" >> "$LOG"
            echo "  $OUTPUT" >> "$LOG"
            cp "$MUTANT" "$CRASH_DIR/crash_$i.ll"
        elif [ $RC -ne 1 ]; then
            # RC=1 is the documented bad-input exit; any other non-zero
            # code without a crash signature is still unexpected.
            CRASHES=$((CRASHES + 1))
            echo "[CRASH] Iteration $i unexpected RC=$RC" >> "$LOG"
            cp "$MUTANT" "$CRASH_DIR/crash_$i.ll"
        else
            PASS=$((PASS + 1))
        fi
    else
        # RC=0 must produce parseable JSON with the events/summary contract
        if [ ! -f "$OUTFILE/history.json" ]; then
            CRASHES=$((CRASHES + 1))
            echo "[CORRUPT] Iteration $i RC=0 but history.json missing" >> "$LOG"
            cp "$MUTANT" "$CRASH_DIR/corrupt_$i.ll"
        elif command -v python3 &>/dev/null && ! python3 -c "import json; d=json.load(open(\"$OUTFILE/history.json\")); assert 'events' in d and 'summary' in d" >/dev/null 2>&1; then
            CRASHES=$((CRASHES + 1))
            echo "[CORRUPT] Iteration $i RC=0 but history.json invalid" >> "$LOG"
            cp "$MUTANT" "$CRASH_DIR/corrupt_$i.ll"
        else
            PASS=$((PASS + 1))
        fi
    fi
    
    # Cleanup
    rm -f "$MUTANT"
    rm -rf "$OUTFILE"
    
    # Progress
    if [ $((i % 50)) -eq 0 ]; then
        echo "  Progress: $i/$ITERATIONS (pass=$PASS crashes=$CRASHES timeouts=$TIMEOUTS)" >> "$LOG"
    fi
done

echo "" >> "$LOG"
echo "=== Fuzz Results ===" >> "$LOG"
echo "  Total: $ITERATIONS" >> "$LOG"
echo "  Passed: $PASS" >> "$LOG"
echo "  Crashes: $CRASHES" >> "$LOG"
echo "  Timeouts: $TIMEOUTS" >> "$LOG"

if [ $CRASHES -gt 0 ]; then
    echo "  *** CRASHES DETECTED — see $CRASH_DIR ***" >> "$LOG"
fi

echo ""
cat "$LOG"
