# SPDX-License-Identifier: GPL-2.0

CFLAGS += -O3 -Wl,-no-as-needed -Wall -I $(top_srcdir)/usr/include
ifneq ($(WERROR),0)
	CFLAGS += -Werror
endif

LDLIBS += -lpthread -lm -luring

TEST_PROGS := test_generic_02.sh
TEST_PROGS += test_generic_03.sh
TEST_PROGS += test_generic_06.sh
TEST_PROGS += test_generic_07.sh

TEST_PROGS += test_generic_08.sh
TEST_PROGS += test_generic_09.sh
TEST_PROGS += test_generic_10.sh
TEST_PROGS += test_generic_12.sh
TEST_PROGS += test_generic_13.sh
TEST_PROGS += test_generic_16.sh

TEST_PROGS += test_batch_01.sh
TEST_PROGS += test_batch_02.sh
TEST_PROGS += test_batch_03.sh

TEST_PROGS += test_null_01.sh
TEST_PROGS += test_null_02.sh
TEST_PROGS += test_null_03.sh
TEST_PROGS += test_loop_01.sh
TEST_PROGS += test_loop_02.sh
TEST_PROGS += test_loop_03.sh
TEST_PROGS += test_loop_04.sh
TEST_PROGS += test_loop_05.sh
TEST_PROGS += test_loop_06.sh
TEST_PROGS += test_loop_07.sh

TEST_PROGS += test_integrity_01.sh
TEST_PROGS += test_integrity_02.sh

TEST_PROGS += test_recover_01.sh
TEST_PROGS += test_recover_02.sh
TEST_PROGS += test_recover_03.sh
TEST_PROGS += test_recover_04.sh
TEST_PROGS += test_stripe_01.sh
TEST_PROGS += test_stripe_02.sh
TEST_PROGS += test_stripe_03.sh
TEST_PROGS += test_stripe_04.sh
TEST_PROGS += test_stripe_05.sh
TEST_PROGS += test_stripe_06.sh

TEST_PROGS += test_part_01.sh
TEST_PROGS += test_part_02.sh

TEST_PROGS += test_stress_01.sh
TEST_PROGS += test_stress_02.sh
TEST_PROGS += test_stress_03.sh
TEST_PROGS += test_stress_04.sh
TEST_PROGS += test_stress_05.sh
TEST_PROGS += test_stress_06.sh
TEST_PROGS += test_stress_07.sh
TEST_PROGS += test_stress_08.sh
TEST_PROGS += test_stress_09.sh

TEST_FILES := settings

TEST_GEN_PROGS_EXTENDED = kublk metadata_size
STANDALONE_UTILS := metadata_size.c

LOCAL_HDRS += $(wildcard *.h)
include ../lib.mk

$(OUTPUT)/kublk: $(filter-out $(STANDALONE_UTILS),$(wildcard *.c))

check:
	shellcheck -x -f gcc *.sh

# Test groups for running subsets of tests
# JOBS=1 (default): sequential with kselftest TAP output
# JOBS>1: parallel execution with xargs -P
# Usage: make run_null JOBS=4
JOBS ?= 1
export JOBS

# Auto-detect test groups from TEST_PROGS (test_<group>_<num>.sh -> group)
TEST_GROUPS := $(shell echo "$(TEST_PROGS)" | tr ' ' '\n' | \
	sed 's/test_\([^_]*\)_.*/\1/' | sort -u)

# Template for group test targets
# $(1) = group name (e.g., null, generic, stress)
define RUN_GROUP
run_$(1): all
	@if [ $$(JOBS) -gt 1 ]; then \
		echo $$(filter test_$(1)_%.sh,$$(TEST_PROGS)) | tr ' ' '\n' | \
			xargs -P $$(JOBS) -n1 sh -c './"$$$$0"' || true; \
	else \
		$$(call RUN_TESTS, $$(filter test_$(1)_%.sh,$$(TEST_PROGS))); \
	fi
.PHONY: run_$(1)
endef

# Generate targets for each discovered test group
$(foreach group,$(TEST_GROUPS),$(eval $(call RUN_GROUP,$(group))))

# Run all tests (parallel when JOBS>1)
run_all: all
	@if [ $(JOBS) -gt 1 ]; then \
		echo $(TEST_PROGS) | tr ' ' '\n' | \
			xargs -P $(JOBS) -n1 sh -c './"$$0"' || true; \
	else \
		$(call RUN_TESTS, $(TEST_PROGS)); \
	fi
.PHONY: run_all
