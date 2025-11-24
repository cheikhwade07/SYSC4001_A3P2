#!/usr/bin/env bash
#
# Run all Part A tests for SYSC4001 Assignment 3 – Part 2
# This script automatically:
#   • cleans + rebuilds the program
#   • resets the rubric
#   • runs multiple TA configurations (2, 3, 5 TAs)
#   • saves all outputs to tests/*.log
#

set -e   # Stop on errors

# Resolve project directory (the 'A' folder)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"   # → .../SYSC4001_A3_P2/A

EXE="$PROJECT_DIR/marker"
RUBRIC="$PROJECT_DIR/data/rubric.txt"
EXAMS="$PROJECT_DIR/data/exams"
TESTDIR="$PROJECT_DIR/tests"

echo "Running tests in: $PROJECT_DIR"
mkdir -p "$TESTDIR"

# Function to run one test
run_test() {
    local NUM_TAS="$1"
    local LOGFILE="$TESTDIR/partA_${NUM_TAS}TAs.log"

    echo "-------------------------------------------------------"
    echo " Running Part A test with $NUM_TAS TA processes..."
    echo " Output → $LOGFILE"
    echo "-------------------------------------------------------"

    # Reset rubric & rebuild from project dir (A/)
    (
        cd "$PROJECT_DIR" || exit 1
        make clean
        make reset_rubric
        make
    )

    # Run test and capture output
    "$EXE" "$NUM_TAS" "$RUBRIC" "$EXAMS" > "$LOGFILE" 2>&1
}

# Test cases
run_test 2
run_test 3
run_test 5

echo ""
echo "All tests completed successfully."
echo "Logs stored under: $TESTDIR/"
