CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -std=c11 -O2 -g
CFLAGS  += -D_POSIX_C_SOURCE=200809L -pthread
LDLIBS  += -pthread -lrt
TARGET  := campuscompute
SRCS    := $(wildcard *.c)
OBJS    := $(SRCS:.c=.o)

.PHONY: all clean test testb

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c campuscompute.h partb.h
	$(CC) $(CFLAGS) -c -o $@ $<

# Removes build output and the log file this program writes (nothing else).
clean:
	rm -f $(OBJS) $(TARGET) campuscompute.log

# Part A self-test: valid commands must succeed, invalid ones must fail.
test: $(TARGET)
	./$(TARGET) fcfs samples/jobs.txt
	./$(TARGET) rr samples/jobs.txt 3
	./$(TARGET) priority samples/jobs.txt
	./$(TARGET) compare samples/jobs.txt 4
	./$(TARGET) fcfs samples/jobs_idle.txt
	./$(TARGET) banker safe
	./$(TARGET) banker unsafe
	./$(TARGET) banker samples/banker_safe.txt
	./$(TARGET) banker samples/banker_unsafe.txt
	! ./$(TARGET) bogus
	! ./$(TARGET) rr samples/jobs.txt 0
	! ./$(TARGET) fcfs samples/jobs_bad.txt
	! ./$(TARGET) banker nofile.txt
	@echo "Part A tests passed"

# Part B self-test: every feature must succeed; bad usage must fail.
testb: $(TARGET)
	./$(TARGET) logger
	./$(TARGET) ipc
	./$(TARGET) worker normal
	./$(TARGET) worker fail
	./$(TARGET) rlimit
	CAMPUS_KEEP_IPC=1 ./$(TARGET) ipc
	./$(TARGET) cleanup
	! ./$(TARGET) worker bogus
	! ./$(TARGET) logger extra
	@echo "Part B tests passed"
