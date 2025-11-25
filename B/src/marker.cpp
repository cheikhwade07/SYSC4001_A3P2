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
#include <semaphore.h>
#include <cstdarg> 

#define NUM_QUESTIONS 5

// Shared memory structure
struct SharedArea {
    char rubric[NUM_QUESTIONS];         // Rubric letters stored in shared memory
    int  question_state[NUM_QUESTIONS]; // 0 = not started, 1 = marking, 2 = done
    char student_id[5];                 // "0001" - 4 digits + '\0'
    int  exam_done;                     // 1 = current exam appears fully marked
    int  terminate;                     // 1 = stop signal (student 9999 or no more exams)
    int  rubric_dirty;                  // 1 = rubric changed in SHM, parent must write to file

    int  log_counter;                   // Shared global action counter (to observe interleaving)

    // --- NEW: semaphores for Part B (process-shared) ---
    sem_t mutex_rubric;     // protects rubric[] and rubric_dirty
    sem_t mutex_questions;  // protects question_state[] and exam_done
    sem_t mutex_log;        // protects log_counter and G-printing
    sem_t exam_ready;       // used to wake TAs when a new exam is loaded
};
/**
 * Seydi Cheikh Wade (101323727)
 * Sean Baldaia (101315064)
 */
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
 * Caller must hold mutex_rubric while calling this.
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
 * Helper: log with global G-counter (protected by mutex_log).
 */
static void log_parent(SharedArea *sh, const char *fmt, ...) {
    sem_wait(&sh->mutex_log);
    int log_id = sh->log_counter++;

    std::printf("[G%05d][PARENT] ", log_id);

    va_list args;
    va_start(args, fmt);
    std::vprintf(fmt, args);
    va_end(args);

    std::printf("\n");
    sem_post(&sh->mutex_log);
}

/**
 * Helper: log for a TA.
 */
static void log_ta(SharedArea *sh, int ta_id, const char *fmt, ...) {
    sem_wait(&sh->mutex_log);
    int log_id = sh->log_counter++;

    std::printf("[G%05d][TA %d] ", log_id, ta_id);

    va_list args;
    va_start(args, fmt);
    std::vprintf(fmt, args);
    va_end(args);

    std::printf("\n");
    sem_post(&sh->mutex_log);
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
    std::memcpy(sh->student_id, line, 4); // Copy first 4 chars from line into student_id
    sh->student_id[4] = '\0';

    log_parent(sh, "Loaded exam %02d from %s, student %s", idx, path, sh->student_id);

    std::fclose(f);

    // Reset question states
    for (int i = 0; i < NUM_QUESTIONS; i++) {
        sh->question_state[i] = 0;
    }
    sh->exam_done = 0;

    // Set terminate if this is the sentinel student
    if (std::strncmp(sh->student_id, "9999", 4) == 0) {
        log_parent(sh, "Student 9999 reached. Setting terminate flag.");
        sh->terminate = 1;
    }

    return 0;
}

/**
 * Code executed by each TA process.
 * - Works only with data in shared memory (no direct file I/O).
 * - Reviews rubric, possibly changes entries, and sets rubric_dirty.
 * - Marks questions for current exam, prints actions.
 * - Uses semaphores (no busy waiting) when waiting for next exams.
 */
static void ta_process(int ta_id, SharedArea *sh) {

    // Unique-ish seed per TA
    std::srand(static_cast<unsigned int>(std::time(nullptr) ^ (getpid() << 16)));

    while (true) {
        if (sh->terminate) {
            log_ta(sh, ta_id, "terminate flag set before work, exiting.");
            break;
        }

        log_ta(sh, ta_id, "Starting work on student %s", sh->student_id);

        // 1) Review rubric (IN SHARED MEMORY ONLY, protected by mutex_rubric)
        for (int q = 0; q < NUM_QUESTIONS; q++) {
            // Read current rubric letter under lock
            sem_wait(&sh->mutex_rubric);
            char current = sh->rubric[q];
            sem_post(&sh->mutex_rubric);

            log_ta(sh, ta_id, "Checking rubric for Q%d (current '%c')", q + 1, current);

            // 0.5–1.0 seconds regardless of change or not
            sleep_random_ms(500, 1000);

            // Randomly decide whether to change this rubric entry
            int change = std::rand() % 2;  // 0 or 1
            if (change) {
                sem_wait(&sh->mutex_rubric);
                char old = sh->rubric[q];
                char newc = old + 1;
                if (newc > 'Z') {
                    newc = 'A';  // wrap around to keep it printable
                }
                sh->rubric[q] = newc;
                sh->rubric_dirty = 1; // tell parent to save
                sem_post(&sh->mutex_rubric);

                log_ta(sh, ta_id,
                       "Correcting rubric Q%d: %c -> %c (in shared memory)",
                       q + 1, old, newc);
            } else {
                sem_wait(&sh->mutex_rubric);
                char still = sh->rubric[q];
                sem_post(&sh->mutex_rubric);

                log_ta(sh, ta_id,
                       "Rubric for Q%d unchanged (still '%c')",
                       q + 1, still);
            }
        }

        if (sh->terminate) {
            log_ta(sh, ta_id, "terminate flag set after rubric, exiting.");
            break;
        }

        // 2) Mark questions for this exam
        while (true) {
            if (sh->terminate) {
                log_ta(sh, ta_id, "terminate flag set while marking, exiting.");
                return;
            }

            int q_to_mark = -1;
            bool all_done = true;

            // Critical section on question_state + exam_done
            sem_wait(&sh->mutex_questions);
            for (int i = 0; i < NUM_QUESTIONS; i++) {
                if (sh->question_state[i] == 0) {
                    // claim this question
                    q_to_mark = i;
                    sh->question_state[i] = 1; // marking in progress
                    all_done = false;
                    break;
                }
                if (sh->question_state[i] != 2) {
                    all_done = false;
                }
            }

            if (q_to_mark == -1 && all_done) {
                sh->exam_done = 1;
            }
            sem_post(&sh->mutex_questions);

            if (q_to_mark == -1) {
                if (all_done) {
                    log_ta(sh, ta_id,
                           "All questions for student %s appear done.",
                           sh->student_id);
                    break;
                } else {
                    // someone else is still marking, check again later
                    usleep(100 * 1000); // 100 ms
                    continue;
                }
            }

            // We claimed question q_to_mark; mark it using current rubric
            sem_wait(&sh->mutex_rubric);
            char mark_letter = sh->rubric[q_to_mark];
            sem_post(&sh->mutex_rubric);

            log_ta(sh, ta_id,
                   "Marking Q%d for student %s (rubric '%c')",
                   q_to_mark + 1, sh->student_id, mark_letter);

            // Marking time: 1.0–2.0 seconds
            sleep_random_ms(1000, 2000);

            // Now set the question as done
            sem_wait(&sh->mutex_questions);
            sh->question_state[q_to_mark] = 2;
            sem_post(&sh->mutex_questions);

            log_ta(sh, ta_id,
                   "Finished Q%d for student %s",
                   q_to_mark + 1, sh->student_id);
        }

        // 3) Wait for next exam to be loaded (no busy-wait)
        log_ta(sh, ta_id, "Waiting for next exam...");

        // Block here until parent signals a new exam, or terminate is set
        sem_wait(&sh->exam_ready);
        if (sh->terminate) {
            log_ta(sh, ta_id, "woken up but terminate flag set, exiting.");
            break;
        }
        // Otherwise, loop continues and TA will see new student_id, etc.
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
    sh->log_counter  = 0;

    // Initialize semaphores (pshared = 1 -> shared between processes)
    if (sem_init(&sh->mutex_rubric,    1, 1) == -1 ||
        sem_init(&sh->mutex_questions, 1, 1) == -1 ||
        sem_init(&sh->mutex_log,       1, 1) == -1 ||
        sem_init(&sh->exam_ready,      1, 0) == -1) {
        std::perror("sem_init");
        shmdt(sh);
        shmctl(shmid, IPC_RMID, nullptr);
        return EXIT_FAILURE;
    }

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
        bool exam_done_copy = false;

        sem_wait(&sh->mutex_questions);
        exam_done_copy = (sh->exam_done != 0);
        sem_post(&sh->mutex_questions);

        if (exam_done_copy && !sh->terminate) {
            exam_index++;
            if (load_exam(exam_dir, exam_index, sh) != 0) {
                // load_exam sets terminate in error/sentinel cases
                break;
            }

            // Reset exam_done under protection
            sem_wait(&sh->mutex_questions);
            sh->exam_done = 0;
            sem_post(&sh->mutex_questions);

            // Wake each TA once so they can start on this exam
            for (int i = 0; i < num_TAs; i++) {
                sem_post(&sh->exam_ready);
            }
        }

        // If any TA changed the rubric in shared memory, write it to file
        int need_save = 0;
        sem_wait(&sh->mutex_rubric);
        if (sh->rubric_dirty) {
            need_save = 1;
        }
        sem_post(&sh->mutex_rubric);

        if (need_save) {
            log_parent(sh, "Detected rubric change. Saving rubric to file...");
            sem_wait(&sh->mutex_rubric);
            if (save_rubric(rubric_path, sh) != 0) {
                sem_post(&sh->mutex_rubric);
                log_parent(sh, "Failed to save rubric file");
            } else {
                sh->rubric_dirty = 0;
                sem_post(&sh->mutex_rubric);
            }
        }

        usleep(200 * 1000); // 200 ms polling
    }

    // Wake any TAs that might be blocked on exam_ready so they can exit
    for (int i = 0; i < num_TAs; i++) {
        sem_post(&sh->exam_ready);
    }

    log_parent(sh, "Termination condition reached. Waiting for TAs...");

    // Wait for all TA children to exit
    int status;
    while (wait(&status) > 0) {
        // nothing
    }

    log_parent(sh, "All done.");

    // Destroy semaphores (while SHM is still attached)
    sem_destroy(&sh->mutex_rubric);
    sem_destroy(&sh->mutex_questions);
    sem_destroy(&sh->mutex_log);
    sem_destroy(&sh->exam_ready);

    // Clean up shared memory
    shmdt(sh);
    shmctl(shmid, IPC_RMID, nullptr);

    return EXIT_SUCCESS;
}
