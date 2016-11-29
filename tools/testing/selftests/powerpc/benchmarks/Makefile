TEST_GEN_PROGS := gettimeofday context_switch mmap_bench futex_bench null_syscall

CFLAGS += -O2

$(TEST_GEN_PROGS): ../harness.c

include ../../lib.mk

$(OUTPUT)/context_switch: ../utils.c
$(OUTPUT)/context_switch: CFLAGS += -maltivec -mvsx -mabi=altivec
$(OUTPUT)/context_switch: LDLIBS += -lpthread
