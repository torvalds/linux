# The loops are all 64-bit code
CFLAGS += -m64
CFLAGS += -I$(CURDIR)

PROGS := memcmp
EXTRA_SOURCES := memcmp_64.S ../harness.c

all: $(PROGS)

$(PROGS): $(EXTRA_SOURCES)

run_tests: all
	@-for PROG in $(PROGS); do \
		./$$PROG; \
	done;

clean:
	rm -f $(PROGS) *.o

.PHONY: all run_tests clean
