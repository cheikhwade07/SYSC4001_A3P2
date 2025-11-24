# SYSC4001 – Assignment 3 Part 2 (Processes + Shared Memory)

This project implements **Part 2(a)** of the concurrency assignment.

- One process per Teaching Assistant (TA)
- Parent process:
  - Creates a System V shared memory segment.
  - Loads the rubric file and exam files into shared memory.
  - Writes rubric changes from shared memory back to the rubric file.
- TA processes:
  - Work **only** on data stored in shared memory.
  - Review and possibly correct the rubric.
  - Mark questions on the current exam.
  - Print what they are doing (rubric access, marking, etc.).

## Directory layout

```text
SYSC4001_A3_P2/
├── Makefile
├── README.md
├── src/
│   └── marker.c
└── data/
    ├── rubric.txt
    └── exams/
        ├── exam01.txt
        ├── ...
        └── exam20.txt


