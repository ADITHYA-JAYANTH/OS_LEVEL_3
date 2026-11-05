# CampusCompute (OS Level 3 Mini-Project, UE25CS242A)

Command-line prototype in C for Linux / Ubuntu / WSL Ubuntu that demonstrates
job scheduling, a concurrent logger, inter-process communication, resource-safety
checks, worker-failure detection and process resource limits.

## Prerequisites
    sudo apt install build-essential     # gcc, make

## Build / clean / test
    make          # builds ./campuscompute
    make clean    # removes object files, the executable and campuscompute.log
    make test     # Part A self-test (valid commands succeed, invalid ones fail)
    make testb    # Part B self-test (all features succeed, bad usage fails)

## Source files
| File | Contents |
|------|----------|
| `campuscompute.h` | Shared interfaces and input limits |
| `main.c` | Command-line interface, usage text, error handling (FR8) |
| `scheduler.c` | Job model, FCFS, Round Robin, Priority, comparison (FR1, FR2) |
| `banker.c` | Banker's safety algorithm (FR5) |
| `partb.h` | Internal definitions shared by the files below |
| `logger.c` | Producer-consumer logger with pthreads (FR3) |
| `ipc.c` | POSIX shared memory and named semaphores (FR4) |
| `worker.c` | fork() / waitpid() worker processes (FR6) |
| `limits.c` | setrlimit() demonstrations (FR7) |
| `cleanup.c` | Removes and verifies program-created resources (FR7) |
| `integrate.c` | Integrated demonstration with PASS/FAIL summary |
| `samples/` | Repeatable test inputs (job files, Banker's state files) |

## Commands
    ./campuscompute help
    ./campuscompute fcfs     [jobfile]
    ./campuscompute rr       [jobfile] [quantum]     # default quantum 4
    ./campuscompute priority [jobfile]
    ./campuscompute compare  [jobfile] [quantum]     # all three, same job set
    ./campuscompute banker   safe | unsafe | <statefile>
    ./campuscompute logger                           # producer-consumer logger
    ./campuscompute ipc                              # shared memory + semaphores
    ./campuscompute worker   normal | fail           # 'fail' = SIMULATED failures
    ./campuscompute rlimit                           # setrlimit() demo
    ./campuscompute cleanup                          # remove and verify our IPC objects
    ./campuscompute partA                            # compare + both Banker cases
    ./campuscompute all                              # integrated demo with summary

Default job file: `samples/jobs.txt`. Invalid commands or arguments print an error
and a hint to run `help`.

### Job file format
One job per line: `id arrival burst priority`. `#` starts a comment.
IDs unique (1-9999), arrival 0-100000, burst 1-10000, priority 0-100
(lower number = higher priority). Bad lines report `file:line` and the reason.

### Banker's state file format
    n m
    Available (m values)
    Allocation (n rows of m values)
    Maximum    (n rows of m values)
Need = Maximum - Allocation is computed by the program.

## Scheduling rules
- **FCFS**: earlier arrival first; equal arrival -> lower job ID.
- **Priority (non-preemptive)**: lowest priority number first; ties -> earlier arrival -> lower job ID.
- **Round Robin**: FIFO queue, quantum given on the command line (default 4).
  Jobs arriving during a slice are queued before the preempted job.
- CPU idle gaps appear as `IDLE` in the timeline.

## Design notes (Part B)
- **Logger:** circular buffer of 5 entries, 3 producer threads, 1 consumer thread. One mutex
  guards the buffer; condition variables `not_full` and `not_empty` coordinate waiting (always
  inside `while` loops). The consumer exits when the queue is empty and all producers are done;
  `main` joins every thread. Output file: `campuscompute.log`.
- **IPC:** a child writes CPU-burst values into POSIX shared memory (`shm_open`, `mmap`) and
  signals the parent with named semaphore `/campuscompute_sem_req`; the parent writes the total
  and average back and signals `/campuscompute_sem_ack`. Waits use `sem_timedwait` (5 s) so a
  dead peer cannot hang the program.
- **Workers:** three children created with `fork()` and collected with `waitpid()`, decoded with
  `WIFEXITED`, `WEXITSTATUS`, `WIFSIGNALED`, `WTERMSIG`. In `fail` mode worker 2 exits with status
  42 and worker 3 raises SIGKILL on itself. **These failures are simulated on purpose; they are
  not real OS failures**, and the output says so.
- **Resource limits:** run in forked children so the parent is unaffected. `RLIMIT_AS` (128 MB):
  a 256 MB `malloc` fails while a 16 MB one succeeds. `RLIMIT_CPU` (1 s): a busy-looping child is
  terminated by SIGXCPU. `RLIMIT_CORE` is set to 0 first so no core file is created.

## Resources created and how they are released
| Resource | Created by | Released by |
|----------|-----------|-------------|
| threads (3 producers, 1 consumer) | `logger` | joined before the command returns |
| child processes | `ipc`, `worker`, `rlimit` | `waitpid()` in the same command |
| `/dev/shm/campuscompute_shm` | `ipc` | `ipc` itself, or `./campuscompute cleanup` |
| `/dev/shm/sem.campuscompute_sem_req`, `/dev/shm/sem.campuscompute_sem_ack` | `ipc` | `ipc` itself, or `./campuscompute cleanup` |
| `campuscompute.log` | `logger` | `make clean` |

Cleanup only touches the three IPC names above; no other system-wide objects are removed.

To see the cleanup command remove something, leave the IPC objects in place first:

    CAMPUS_KEEP_IPC=1 ./campuscompute ipc
    ls -l /dev/shm | grep campus
    ./campuscompute cleanup
    ls /dev/shm | grep campus

## Screenshots
Before capturing, set the prompt with your own full SRN:

    export SRN='YOUR_FULL_SRN'; export PS1='[$SRN] \w\$ '

| Shot | Command |
|------|---------|
| S01 | `pwd; uname -a` |
| S02 | `gcc --version; make --version` |
| S03 | `ls -l` |
| S04 | `make clean && make` |
| S05 | `./campuscompute help` |
| S06 | `./campuscompute fcfs samples/jobs.txt` |
| S07 | `./campuscompute rr samples/jobs.txt 3` |
| S08 | `./campuscompute priority samples/jobs.txt` |
| S09 | `./campuscompute compare samples/jobs.txt 4` |
| S10 | `./campuscompute logger \| head -22` |
| S11 | `./campuscompute logger \| tail -22` |
| S12 | `./campuscompute ipc` |
| S13 | `CAMPUS_KEEP_IPC=1 ./campuscompute ipc \| tail -6`, `ls -l /dev/shm \| grep campus`, `./campuscompute cleanup \| head -6` |
| S14 | `./campuscompute banker safe` |
| S15 | `./campuscompute banker unsafe` |
| S16 | `./campuscompute worker normal` |
| S17 | `./campuscompute worker fail` |
| S18 | `./campuscompute rlimit` |
| S19 | `./campuscompute bogus` and `./campuscompute fcfs samples/jobs_bad.txt` |
| S20 | `./campuscompute cleanup`, then `ps -ef \| grep '[c]ampuscompute'` and `ls /dev/shm \| grep campus` |
| S21 | `./campuscompute all \| tail -22` |
| S22 | `ls -l campuscompute.log; head -10 campuscompute.log` |
| S23 | `ls -l` (after building) |
| S24 | `unzip -l <SRN>.zip \| tail -25` and `unzip -t <SRN>.zip \| tail -3` |
