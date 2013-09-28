noarg:
	$(MAKE) -C ../

PROGS := count_instructions
EXTRA_SOURCES := ../harness.c event.c

all: $(PROGS)

$(PROGS): $(EXTRA_SOURCES)

# loop.S can only be built 64-bit
count_instructions: loop.S count_instructions.c $(EXTRA_SOURCES)
	$(CC) $(CFLAGS) -m64 -o $@ $^

run_tests: all
	@-for PROG in $(PROGS); do \
		./$$PROG; \
	done;

clean:
	rm -f $(PROGS) loop.o

.PHONY: all run_tests clean
