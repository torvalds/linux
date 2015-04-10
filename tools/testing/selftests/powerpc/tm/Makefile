PROGS := tm-resched-dscr tm-syscall

all: $(PROGS)

$(PROGS): ../harness.c

tm-syscall: tm-syscall-asm.S
tm-syscall: CFLAGS += -mhtm

run_tests: all
	@-for PROG in $(PROGS); do \
		./$$PROG; \
	done;

clean:
	rm -f $(PROGS) *.o

.PHONY: all run_tests clean
