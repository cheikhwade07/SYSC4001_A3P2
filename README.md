# SYSC 4001  Assignment 3, Part 2  
Student 1: Seydi Cheikh Wade (101323727)

Student 2: Sean Baldaia (101315064)

Concurrent TA Marker (Shared Memory + Processes)

This repository contains two implementations of the TA–exam marker system:

- `A/` – Part 2.a: shared memory + multiple processes, **no semaphores**  
- `B/` – Part 2.b: shared memory + **semaphore-based synchronization**

Each part is self-contained (own `Makefile`, `src/`, `data/`, `tests/`).

---

## Requirements

- Linux / WSL / macOS with POSIX support
- `g++` with C++17
- `make`

---

## Part A – Unsynchronized version

```bash
cd A
make          # builds ./marker

```
Run with a chosen number of TA processes N:
```bash
./marker N data/rubric.txt data/exams
```
### example:
```bash
./marker 3 data/rubric.txt data/exams
```

There is also a default target:
```bash
make run      # runs: ./marker 3 data/rubric.txt data/exams
```


Utility targets:
```bash
make reset_rubric   # restore rubric.txt to base A–E
make clean          # remove marker binary and reset rubric
```

## Part B – Semaphore-based version
```bash
cd B
make          # builds ./marker
```

Run with N TA processes (same interface as Part A):
```bash
./marker N data/rubric.txt data/exams
```
### example:
```bash
./marker 5 data/rubric.txt data/exams
```


Default run:
```bash
make run      # runs: ./marker 3 data/rubric.txt data/exams
```

Utility targets:
```
make reset_rubric   # restore rubric.txt
make clean          # remove marker binary and reset rubric
```
## Note:
After every run you should reset by doing:
```bash
make clean          # remove marker binary and reset rubric
make
//Running ./marker N data/rubric.txt data/exams
```
That goes for part  A and B to make sure everyhting run smoothly.


## Test scripts 

Each part has a simple test runner under tests/:
```bash
cd A
chmod +x tests/run_all_tests.sh
./tests/run_all_tests.sh
```
```bash
cd ../B
chmod +x tests/run_all_tests.sh
./tests/run_all_tests.sh
```

These scripts run the program with different numbers of TAs and save the logs.
Those scipt were used to generate the logs that you can see in the directory.


