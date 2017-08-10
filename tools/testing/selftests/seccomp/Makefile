TEST_GEN_PROGS := seccomp_bpf
CFLAGS += -Wl,-no-as-needed -Wall
LDFLAGS += -lpthread

include ../lib.mk

$(TEST_GEN_PROGS): seccomp_bpf.c ../kselftest_harness.h
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@
