#!/bin/bash
#
# Run all Part A tests for SYSC4001 Assignment 3 – Part 2
# - Cleans + rebuilds marker
# - Resets rubric before each run
# - Runs with 2, 3, and 5 TAs
# - Stores logs in tests/partA_*.log
#

set -e  # exit on first error

# Go to the A/ directory (the one that has Makefile, marker, data/, src/, tests/)
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
cd "$PROJECT_DIR"

EXE="./marker"
RUBRIC="data/rubric.txt"
EXAMS="data/exams"
TESTDIR="tests"

echo "Running tests in: $PROJECT_DIR"
mkdir -p "$TESTDIR"

run_test() {
    local NUM_TAS="$1"
    local LOGFILE="$TESTDIR/partB_${NUM_TAS}TAs.log"

    echo "-------------------------------------------------------"
    echo " Running Part B test with $NUM_TAS TA processes..."
    echo " Output → $LOGFILE"
    echo "-------------------------------------------------------"

    # Reset rubric & rebuild from scratch
    make clean
    make reset_rubric
    make

    
   # Run test with line-buffered stdout/stderr
    stdbuf -oL -eL "$EXE" "$NUM_TAS" "$RUBRIC" "$EXAMS" > "$LOGFILE" 2>&1

}

# Run the three scenarios
run_test 2
run_test 3
run_test 5

echo
echo "All tests completed successfully."
echo "Logs stored under: $TESTDIR/"
