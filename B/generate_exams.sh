#!/bin/bash
# SYSC4001 - Assignment 3 Part 2
# Helper script to generate exam files under data/exams/
# instead of creating 20 files manually.

set -e  # exit on first error

BASE_DIR="$(dirname "$0")"
EXAM_DIR="$BASE_DIR/data/exams"

# Ensure target directory exists
mkdir -p "$EXAM_DIR"

echo "Generating exams in: $EXAM_DIR"

# Exams 1–19: student IDs 0001 .. 0019
for i in $(seq 1 19); do
    sid=$(printf "%04d" "$i")      # 1 -> 0001, 2 -> 0002, ...
    file="$EXAM_DIR/exam$(printf "%02d" "$i").txt"

    cat > "$file" << EOF
${sid}
Exam ${sid} contents placeholder.
EOF

    echo "Created $file (student ${sid})"
done


# Exam 20: sentinel student 9999 (stops all TAs)
sentinel="$EXAM_DIR/exam20.txt"
cat > "$sentinel" << 'EOF'
9999
Exam 9999 – sentinel exam, stops all TAs.
EOF

echo "Created $sentinel (student 9999 sentinel)"
echo "Done."
