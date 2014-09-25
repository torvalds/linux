noarg:
	$(MAKE) -C ../

PROGS := count_instructions l3_bank_test per_event_excludes
EXTRA_SOURCES := ../harness.c event.c lib.c

SUB_TARGETS = ebb

all: $(PROGS) $(SUB_TARGETS)

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

$(SUB_TARGETS):
	$(MAKE) -k -C $@ all

sub_run_tests: all
	@for TARGET in $(SUB_TARGETS); do \
		$(MAKE) -C $$TARGET run_tests; \
	done;

sub_clean:
	@for TARGET in $(SUB_TARGETS); do \
		$(MAKE) -C $$TARGET clean; \
	done;

.PHONY: all run_tests clean sub_run_tests sub_clean $(SUB_TARGETS)
