#define _XOPEN_SOURCE 700

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <errno.h>

#define NUM_QUESTIONS 5

// Shared memory structure
struct SharedArea {
    char rubric[NUM_QUESTIONS];         // Rubric letters stored in shared memory
    int  question_state[NUM_QUESTIONS]; // 0 = not started, 1 = marking, 2 = done
    char student_id[5];                 // "0001" - 4 digits + '\0'
    int  exam_done;                     // 1 = current exam appears fully marked
    int  terminate;                     // 1 = stop signal (student 9999 or no more exams)
    int  rubric_dirty;                  // 1 = rubric changed in SHM, parent must write to file

    int  log_counter;   // NEW: shared global action counter (To observe race conditions)
};

/**
 * Sleep for a random number of milliseconds in [min_ms, max_ms].
 */
static void sleep_random_ms(int min_ms, int max_ms) {
    int range = max_ms - min_ms + 1;
    int delay = min_ms + (std::rand() % range);
    usleep(delay * 1000); // convert ms to microseconds
}

/**
 * Load rubric from file into shared memory.
 * Format: "1, A", "2, B", etc. (5 lines).
 * Only the question number and first character after the comma are used.
 */
static int load_rubric(const char *rubric_path, SharedArea *sh) {
    FILE *f = std::fopen(rubric_path, "r");
    if (!f) {
        std::perror("fopen rubric");
        return -1;
    }

    char line[256];
    int qnum;
    char letter;

    for (int i = 0; i < NUM_QUESTIONS; i++) {
        if (!std::fgets(line, sizeof(line), f)) {
            std::fprintf(stderr, "Rubric file has fewer than %d lines\n", NUM_QUESTIONS);
            std::fclose(f);
            return -1;
        }
        if (std::sscanf(line, "%d , %c", &qnum, &letter) != 2 &&
            std::sscanf(line, "%d,%c", &qnum, &letter) != 2) {
            std::fprintf(stderr, "Bad rubric line: %s\n", line);
            std::fclose(f);
            return -1;
        }
        if (qnum < 1 || qnum > NUM_QUESTIONS) {
            std::fprintf(stderr, "Invalid question number in rubric: %d\n", qnum);
            std::fclose(f);
            return -1;
        }
        sh->rubric[qnum - 1] = letter;
    }

    std::fclose(f);
    return 0;
}

/**
 * Save rubric from shared memory back to file.
 */
static int save_rubric(const char *rubric_path, SharedArea *sh) {
    FILE *f = std::fopen(rubric_path, "w");
    if (!f) {
        std::perror("fopen rubric for write");
        return -1;
    }

    for (int i = 0; i < NUM_QUESTIONS; i++) {
        std::fprintf(f, "%d, %c\n", i + 1, sh->rubric[i]);
    }

    std::fclose(f);
    return 0;
}

/**
 * Load exam #idx (1-based) into shared memory.
 * Files are named exam01.txt, exam02.txt, ... inside exam_dir.
 * First line = 4-digit student number.
 *
 * If student number is 9999, terminate flag is set.
 */
static int load_exam(const char *exam_dir, int idx, SharedArea *sh) {
    char path[512];
    std::snprintf(path, sizeof(path), "%s/exam%02d.txt", exam_dir, idx);

    FILE *f = std::fopen(path, "r");
    if (!f) {
        // no more exams or error -> signal terminate
        std::perror("fopen exam");
        sh->terminate = 1;
        return -1;
    }

    char line[256];
    if (!std::fgets(line, sizeof(line), f)) {
        std::fprintf(stderr, "Empty exam file: %s\n", path);
        std::fclose(f);
        sh->terminate = 1;
        return -1;
    }

    // Take the first 4 chars as student ID
    std::memcpy(sh->student_id, line, 4);// Copy first 4 chars from line into student_id 
    sh->student_id[4] = '\0';
    // log loading
    int log_id = sh->log_counter++;   
    std::printf("[G%05d][PARENT] Loaded exam %02d from %s, student %s\n",
                log_id, idx, path, sh->student_id);

    std::fclose(f);

    // Reset question states
    for (int i = 0; i < NUM_QUESTIONS; i++) {
        sh->question_state[i] = 0;
    }
    sh->exam_done = 0;

    // Set terminate if this is the sentinel student
    if (std::strncmp(sh->student_id, "9999", 4) == 0) {
        log_id = sh->log_counter++;
        std::printf("[G%05d][PARENT] Student 9999 reached. Setting terminate flag.\n", log_id);
        sh->terminate = 1;
    }

    return 0;
}

/**
 * Code executed by each TA process.
 * - Works only with data in shared memory (no direct file I/O).
 * - Reviews rubric, possibly changes entries, and sets rubric_dirty.
 * - Marks questions for current exam, prints actions.
 */
static void ta_process(int ta_id, SharedArea *sh) {

    // Unique-ish seed per TA
    std::srand(static_cast<unsigned int>(std::time(nullptr) ^ (getpid() << 16)));

    while (true) {
        if (sh->terminate) {
            int log_id = sh->log_counter++;   // RACE CONDITION (Part A)
            std::printf("[G%05d][TA %d] terminate flag set before work, exiting.\n",
                        log_id, ta_id);
            break;
        }

        int log_id = sh->log_counter++;
        std::printf("[G%05d][TA %d] Starting work on student %s\n",
                    log_id, ta_id, sh->student_id);

        // 1) Review rubric (IN SHARED MEMORY ONLY)
        for (int q = 0; q < NUM_QUESTIONS; q++) {
            char current = sh->rubric[q];

            log_id = sh->log_counter++;
            std::printf("[G%05d][TA %d] Checking rubric for Q%d (current '%c')\n",
                        log_id, ta_id, q + 1, current);

            // 0.5–1.0 seconds regardless of change or not
            sleep_random_ms(500, 1000);

            // Randomly decide whether to change this rubric entry
            int change = std::rand() % 2;  // 0 or 1
            if (change) {
                char old = sh->rubric[q];
                char newc = old + 1;
                if (newc > 'Z') {
                    newc = 'A';  // wrap around to keep it printable
                }
                sh->rubric[q] = newc;
                sh->rubric_dirty = 1; // tell parent to save

                log_id = sh->log_counter++;
                std::printf("[G%05d][TA %d] Correcting rubric Q%d: %c -> %c (in shared memory)\n",
                            log_id, ta_id, q + 1, old, newc);

            } else {
                log_id = sh->log_counter++;
                std::printf("[G%05d][TA %d] Rubric for Q%d unchanged (still '%c')\n",
                            log_id, ta_id, q + 1, sh->rubric[q]);
            }
        }

        if (sh->terminate) {
            int log_id2 = sh->log_counter++;
            std::printf("[G%05d][TA %d] terminate flag set after rubric, exiting.\n",
                        log_id2, ta_id);
            break;
        }

        // 2) Mark questions for this exam
        while (true) {
            if (sh->terminate) {
                int log_id3 = sh->log_counter++;
                std::printf("[G%05d][TA %d] terminate flag set while marking, exiting.\n",
                            log_id3, ta_id);
                return;
            }

            int q_to_mark = -1;

            // Find first question not fully done (state != 2)
            for (int i = 0; i < NUM_QUESTIONS; i++) {
                if (sh->question_state[i] != 2) {
                    q_to_mark = i;
                    break;
                }
            }

            if (q_to_mark == -1) {
                // No questions left to mark; this TA sees exam as done
                sh->exam_done = 1;  // Part (a): race conditions OK
                int log_id4 = sh->log_counter++;
                std::printf("[G%05d][TA %d] All questions for student %s appear done.\n",
                            log_id4, ta_id, sh->student_id);
                break;
            }

            // Try to "claim" this question.
            // Note: No locking here -> race conditions allowed in Part (a).
            if (sh->question_state[q_to_mark] == 2) {
                // Another TA finished it while we were looking, try again
                continue;
            }

            sh->question_state[q_to_mark] = 1; // marking in progress

            int log_id5 = sh->log_counter++;
            std::printf("[G%05d][TA %d] Marking Q%d for student %s (rubric '%c')\n",
                        log_id5, ta_id, q_to_mark + 1, sh->student_id,
                        sh->rubric[q_to_mark]);

            // Marking time: 1.0–2.0 seconds
            sleep_random_ms(1000, 2000);

            sh->question_state[q_to_mark] = 2;

            int log_id6 = sh->log_counter++;
            std::printf("[G%05d][TA %d] Finished Q%d for student %s\n",
                        log_id6, ta_id, q_to_mark + 1, sh->student_id);
        }

        // 3) Wait for next exam to be loaded (busy-wait for Part (a))
        int log_id7 = sh->log_counter++;
        std::printf("[G%05d][TA %d] Waiting for next exam...\n",
                    log_id7, ta_id);

        while (!sh->terminate && sh->exam_done) {
            usleep(100 * 1000); // 100 ms
        }
    }
}


int main(int argc, char *argv[]) {
    if (argc != 4) {
        std::fprintf(stderr,
                     "Usage: %s <num_TAs> <rubric_file> <exam_dir>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int num_TAs = std::atoi(argv[1]);
    if (num_TAs < 2) {
        std::fprintf(stderr, "num_TAs must be >= 2\n");
        return EXIT_FAILURE;
    }

    const char *rubric_path = argv[2];
    const char *exam_dir    = argv[3];

    // Create shared memory segment
    int shmid = shmget(IPC_PRIVATE, sizeof(SharedArea),
                       IPC_CREAT | 0600);
    if (shmid < 0) {
        std::perror("shmget");
        return EXIT_FAILURE;
    }

    SharedArea *sh = static_cast<SharedArea *>(shmat(shmid, nullptr, 0));
    if (sh == reinterpret_cast<void *>(-1)) {
        std::perror("shmat");
        shmctl(shmid, IPC_RMID, nullptr);
        return EXIT_FAILURE;
    }

    // Initialize shared memory
    std::memset(sh, 0, sizeof(*sh));
    sh->terminate    = 0;
    sh->exam_done    = 0;
    sh->rubric_dirty = 0;
    sh->log_counter = 0;  // NEW shared counter

    // Load rubric into shared memory
    if (load_rubric(rubric_path, sh) != 0) {
        std::fprintf(stderr, "Failed to load rubric\n");
        shmdt(sh);
        shmctl(shmid, IPC_RMID, nullptr);
        return EXIT_FAILURE;
    }

    // Load first exam
    int exam_index = 1;
    if (load_exam(exam_dir, exam_index, sh) != 0) {
        std::fprintf(stderr, "Failed to load first exam\n");
        shmdt(sh);
        shmctl(shmid, IPC_RMID, nullptr);
        return EXIT_FAILURE;
    }

    // Fork TA processes
    for (int i = 0; i < num_TAs; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            std::perror("fork");
        } else if (pid == 0) {
            // Child TA process
            SharedArea *child_sh =
                static_cast<SharedArea *>(shmat(shmid, nullptr, 0));
            if (child_sh == reinterpret_cast<void *>(-1)) {
                std::perror("shmat in child");
                std::exit(EXIT_FAILURE);
            }
            ta_process(i, child_sh);
            shmdt(child_sh);
            std::exit(EXIT_SUCCESS);
        }
        // Parent continues loop
    }

    // Parent loop: coordinate exams and file I/O
    std::srand(static_cast<unsigned int>(std::time(nullptr)));

    while (!sh->terminate) {
        // If current exam is done (as seen by some TA), load the next one
        if (sh->exam_done && !sh->terminate) {
            exam_index++;
            if (load_exam(exam_dir, exam_index, sh) != 0) {
                // load_exam sets terminate in error/sentinel cases
                break;
            }
            sh->exam_done = 0;
        }

        // If any TA changed the rubric in shared memory, write it to file
        if (sh->rubric_dirty) {
            int log_id = sh->log_counter++;   // <-- RACE CONDITION check
            std::printf("[G%05d][PARENT] Detected rubric change. Saving rubric to file...\n", log_id);
            if (save_rubric(rubric_path, sh) != 0) {
                log_id = sh->log_counter++;
                std::fprintf(stderr, "[G%05d][PARENT] Failed to save rubric file\n", log_id);
            }
            sh->rubric_dirty = 0;
        }

        usleep(200 * 1000); // 200 ms polling
    }
     int log_id = sh->log_counter++;   // <-- RACE CONDITION check
    std::printf("[G%05d][PARENT] Termination condition reached. Waiting for TAs...\n", log_id);

    // Wait for all TA children to exit
    int status;
    while (wait(&status) > 0) {
        // nothing
    }
    // Log "All done" while SHM is still attached
    log_id = sh->log_counter++;
    std::printf("[G%05d][PARENT] All done.\n", log_id);

    // Clean up shared memory
    shmdt(sh);
    shmctl(shmid, IPC_RMID, nullptr);
 
    return EXIT_SUCCESS;
}
/**
 * Seydi Cheikh Wade (101323727)
 * Sean Baldaia (101315064)
 */
