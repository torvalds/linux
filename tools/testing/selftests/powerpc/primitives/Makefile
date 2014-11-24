CFLAGS += -I$(CURDIR)

PROGS := load_unaligned_zeropad

all: $(PROGS)

$(PROGS): ../harness.c

run_tests: all
	@-for PROG in $(PROGS); do \
		./$$PROG; \
	done;

clean:
	rm -f $(PROGS) *.o

.PHONY: all run_tests clean
