noarg:
	$(MAKE) -C ../

TEST_GEN_PROGS := count_instructions l3_bank_test per_event_excludes
EXTRA_SOURCES := ../harness.c event.c lib.c ../utils.c

include ../../lib.mk

all: $(TEST_GEN_PROGS) ebb

$(TEST_GEN_PROGS): $(EXTRA_SOURCES)

# loop.S can only be built 64-bit
$(OUTPUT)/count_instructions: loop.S count_instructions.c $(EXTRA_SOURCES)
	$(CC) $(CFLAGS) -m64 -o $@ $^

$(OUTPUT)/per_event_excludes: ../utils.c

DEFAULT_RUN_TESTS := $(RUN_TESTS)
override define RUN_TESTS
	$(DEFAULT_RUN_TESTS)
	TARGET=ebb; BUILD_TARGET=$$OUTPUT/$$TARGET; $(MAKE) OUTPUT=$$BUILD_TARGET -C $$TARGET run_tests
endef

DEFAULT_EMIT_TESTS := $(EMIT_TESTS)
override define EMIT_TESTS
	$(DEFAULT_EMIT_TESTS)
	TARGET=ebb; BUILD_TARGET=$$OUTPUT/$$TARGET; $(MAKE) OUTPUT=$$BUILD_TARGET -s -C $$TARGET emit_tests
endef

DEFAULT_INSTALL_RULE := $(INSTALL_RULE)
override define INSTALL_RULE
	$(DEFAULT_INSTALL_RULE)
	TARGET=ebb; BUILD_TARGET=$$OUTPUT/$$TARGET; $(MAKE) OUTPUT=$$BUILD_TARGET -C $$TARGET install
endef

clean:
	$(RM) $(TEST_GEN_PROGS) $(OUTPUT)/loop.o
	TARGET=ebb; BUILD_TARGET=$$OUTPUT/$$TARGET; $(MAKE) OUTPUT=$$BUILD_TARGET -C $$TARGET clean

ebb:
	TARGET=$@; BUILD_TARGET=$$OUTPUT/$$TARGET; mkdir -p $$BUILD_TARGET; $(MAKE) OUTPUT=$$BUILD_TARGET -k -C $$TARGET all

.PHONY: all run_tests clean ebb
