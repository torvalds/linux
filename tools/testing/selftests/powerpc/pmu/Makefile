noarg:
	$(MAKE) -C ../

TEST_PROGS := count_instructions l3_bank_test per_event_excludes
EXTRA_SOURCES := ../harness.c event.c lib.c ../utils.c

all: $(TEST_PROGS) ebb

$(TEST_PROGS): $(EXTRA_SOURCES)

# loop.S can only be built 64-bit
count_instructions: loop.S count_instructions.c $(EXTRA_SOURCES)
	$(CC) $(CFLAGS) -m64 -o $@ $^

per_event_excludes: ../utils.c

include ../../lib.mk

DEFAULT_RUN_TESTS := $(RUN_TESTS)
override define RUN_TESTS
	$(DEFAULT_RUN_TESTS)
	$(MAKE) -C ebb run_tests
endef

DEFAULT_EMIT_TESTS := $(EMIT_TESTS)
override define EMIT_TESTS
	$(DEFAULT_EMIT_TESTS)
	$(MAKE) -s -C ebb emit_tests
endef

DEFAULT_INSTALL_RULE := $(INSTALL_RULE)
override define INSTALL_RULE
	$(DEFAULT_INSTALL_RULE)
	$(MAKE) -C ebb install
endef

clean:
	rm -f $(TEST_PROGS) loop.o
	$(MAKE) -C ebb clean

ebb:
	$(MAKE) -k -C $@ all

.PHONY: all run_tests clean ebb
