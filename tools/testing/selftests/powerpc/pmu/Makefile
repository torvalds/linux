# SPDX-License-Identifier: GPL-2.0
noarg:
	$(MAKE) -C ../

TEST_GEN_PROGS := count_instructions count_stcx_fail l3_bank_test per_event_excludes
EXTRA_SOURCES := ../harness.c event.c lib.c ../utils.c

top_srcdir = ../../../../..
include ../../lib.mk

all: $(TEST_GEN_PROGS) ebb sampling_tests event_code_tests

$(TEST_GEN_PROGS): $(EXTRA_SOURCES)

# loop.S can only be built 64-bit
$(OUTPUT)/count_instructions: CFLAGS += -m64
$(OUTPUT)/count_instructions: loop.S count_instructions.c $(EXTRA_SOURCES)

$(OUTPUT)/count_stcx_fail: CFLAGS += -m64
$(OUTPUT)/count_stcx_fail: loop.S $(EXTRA_SOURCES)


$(OUTPUT)/per_event_excludes: ../utils.c

DEFAULT_RUN_TESTS := $(RUN_TESTS)
override define RUN_TESTS
	$(DEFAULT_RUN_TESTS)
	TARGET=ebb; BUILD_TARGET=$$OUTPUT/$$TARGET; $(MAKE) OUTPUT=$$BUILD_TARGET -C $$TARGET run_tests
	TARGET=sampling_tests; BUILD_TARGET=$$OUTPUT/$$TARGET; $(MAKE) OUTPUT=$$BUILD_TARGET -C $$TARGET run_tests
	TARGET=event_code_tests; BUILD_TARGET=$$OUTPUT/$$TARGET; $(MAKE) OUTPUT=$$BUILD_TARGET -C $$TARGET run_tests
endef

DEFAULT_EMIT_TESTS := $(EMIT_TESTS)
override define EMIT_TESTS
	$(DEFAULT_EMIT_TESTS)
	TARGET=ebb; BUILD_TARGET=$$OUTPUT/$$TARGET; $(MAKE) OUTPUT=$$BUILD_TARGET -s -C $$TARGET emit_tests
	TARGET=sampling_tests; BUILD_TARGET=$$OUTPUT/$$TARGET; $(MAKE) OUTPUT=$$BUILD_TARGET -s -C $$TARGET emit_tests
	TARGET=event_code_tests; BUILD_TARGET=$$OUTPUT/$$TARGET; $(MAKE) OUTPUT=$$BUILD_TARGET -s -C $$TARGET emit_tests
endef

DEFAULT_INSTALL_RULE := $(INSTALL_RULE)
override define INSTALL_RULE
	$(DEFAULT_INSTALL_RULE)
	TARGET=ebb; BUILD_TARGET=$$OUTPUT/$$TARGET; $(MAKE) OUTPUT=$$BUILD_TARGET -C $$TARGET install
	TARGET=sampling_tests; BUILD_TARGET=$$OUTPUT/$$TARGET; $(MAKE) OUTPUT=$$BUILD_TARGET -C $$TARGET install
	TARGET=event_code_tests; BUILD_TARGET=$$OUTPUT/$$TARGET; $(MAKE) OUTPUT=$$BUILD_TARGET -C $$TARGET install
endef

clean:
	$(RM) $(TEST_GEN_PROGS) $(OUTPUT)/loop.o
	TARGET=ebb; BUILD_TARGET=$$OUTPUT/$$TARGET; $(MAKE) OUTPUT=$$BUILD_TARGET -C $$TARGET clean
	TARGET=sampling_tests; BUILD_TARGET=$$OUTPUT/$$TARGET; $(MAKE) OUTPUT=$$BUILD_TARGET -C $$TARGET clean
	TARGET=event_code_tests; BUILD_TARGET=$$OUTPUT/$$TARGET; $(MAKE) OUTPUT=$$BUILD_TARGET -C $$TARGET clean

ebb:
	TARGET=$@; BUILD_TARGET=$$OUTPUT/$$TARGET; mkdir -p $$BUILD_TARGET; $(MAKE) OUTPUT=$$BUILD_TARGET -k -C $$TARGET all

sampling_tests:
	TARGET=$@; BUILD_TARGET=$$OUTPUT/$$TARGET; mkdir -p $$BUILD_TARGET; $(MAKE) OUTPUT=$$BUILD_TARGET -k -C $$TARGET all

event_code_tests:
	TARGET=$@; BUILD_TARGET=$$OUTPUT/$$TARGET; mkdir -p $$BUILD_TARGET; $(MAKE) OUTPUT=$$BUILD_TARGET -k -C $$TARGET all

.PHONY: all run_tests clean ebb sampling_tests event_code_tests
