noarg:
	$(MAKE) -C ../

PROGS := count_instructions
EXTRA_SOURCES := ../harness.c event.c

all: $(PROGS) sub_all

$(PROGS): $(EXTRA_SOURCES)

# loop.S can only be built 64-bit
count_instructions: loop.S count_instructions.c $(EXTRA_SOURCES)
	$(CC) $(CFLAGS) -m64 -o $@ $^

run_tests: all sub_run_tests
	@-for PROG in $(PROGS); do \
		./$$PROG; \
	done;

clean: sub_clean
	rm -f $(PROGS) loop.o


SUB_TARGETS = ebb

sub_all:
	@for TARGET in $(SUB_TARGETS); do \
		$(MAKE) -C $$TARGET all; \
	done;

sub_run_tests: all
	@for TARGET in $(SUB_TARGETS); do \
		$(MAKE) -C $$TARGET run_tests; \
	done;

sub_clean:
	@for TARGET in $(SUB_TARGETS); do \
		$(MAKE) -C $$TARGET clean; \
	done;

.PHONY: all run_tests clean sub_all sub_run_tests sub_clean
