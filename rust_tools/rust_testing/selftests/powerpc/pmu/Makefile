# SPDX-License-Identifier: GPL-2.0
noarg:
	$(MAKE) -C ../

TEST_GEN_PROGS := count_instructions count_stcx_fail l3_bank_test per_event_excludes
EXTRA_SOURCES := ../harness.c event.c lib.c ../utils.c

top_srcdir = ../../../../..
include ../../lib.mk
include ../flags.mk

SUB_DIRS := ebb sampling_tests event_code_tests

all: $(TEST_GEN_PROGS) $(SUB_DIRS)

$(TEST_GEN_PROGS): $(EXTRA_SOURCES)

# loop.S can only be built 64-bit
$(OUTPUT)/count_instructions: CFLAGS += -m64
$(OUTPUT)/count_instructions: loop.S count_instructions.c $(EXTRA_SOURCES)

$(OUTPUT)/count_stcx_fail: CFLAGS += -m64
$(OUTPUT)/count_stcx_fail: loop.S $(EXTRA_SOURCES)


$(OUTPUT)/per_event_excludes: ../utils.c

$(SUB_DIRS):
	BUILD_TARGET=$(OUTPUT)/$@; mkdir -p $$BUILD_TARGET; $(MAKE) OUTPUT=$$BUILD_TARGET -k -C $@ all

DEFAULT_RUN_TESTS := $(RUN_TESTS)
override define RUN_TESTS
	$(DEFAULT_RUN_TESTS)
	+@for TARGET in $(SUB_DIRS); do \
		BUILD_TARGET=$(OUTPUT)/$$TARGET; \
		$(MAKE) OUTPUT=$$BUILD_TARGET -C $$TARGET run_tests; \
	done;
endef

emit_tests:
	for TEST in $(TEST_GEN_PROGS); do \
		BASENAME_TEST=`basename $$TEST`;	\
		echo "$(COLLECTION):$$BASENAME_TEST";	\
	done
	+@for TARGET in $(SUB_DIRS); do \
		BUILD_TARGET=$(OUTPUT)/$$TARGET; \
		$(MAKE) OUTPUT=$$BUILD_TARGET COLLECTION=$(COLLECTION)/$$TARGET -s -C $$TARGET emit_tests; \
	done;

DEFAULT_INSTALL_RULE := $(INSTALL_RULE)
override define INSTALL_RULE
	$(DEFAULT_INSTALL_RULE)
	+@for TARGET in $(SUB_DIRS); do \
		BUILD_TARGET=$(OUTPUT)/$$TARGET; \
		$(MAKE) OUTPUT=$$BUILD_TARGET INSTALL_PATH=$$INSTALL_PATH/$$TARGET -C $$TARGET install; \
	done;
endef

DEFAULT_CLEAN := $(CLEAN)
override define CLEAN
	$(DEFAULT_CLEAN)
	$(RM) $(TEST_GEN_PROGS) $(OUTPUT)/loop.o
	+@for TARGET in $(SUB_DIRS); do \
		BUILD_TARGET=$(OUTPUT)/$$TARGET; \
		$(MAKE) OUTPUT=$$BUILD_TARGET -C $$TARGET clean; \
	done;
endef


.PHONY: all run_tests ebb sampling_tests event_code_tests emit_tests
