# This mimics the top-level Makefile. We do it explicitly here so that this
# Makefile can operate with or without the kbuild infrastructure.
CC := $(CROSS_COMPILE)gcc

ifeq (0,$(MAKELEVEL))
OUTPUT := $(shell pwd)
endif

# The following are built by lib.mk common compile rules.
# TEST_CUSTOM_PROGS should be used by tests that require
# custom build rule and prevent common build rule use.
# TEST_PROGS are for test shell scripts.
# TEST_CUSTOM_PROGS and TEST_PROGS will be run by common run_tests
# and install targets. Common clean doesn't touch them.
TEST_GEN_PROGS := $(patsubst %,$(OUTPUT)/%,$(TEST_GEN_PROGS))
TEST_GEN_PROGS_EXTENDED := $(patsubst %,$(OUTPUT)/%,$(TEST_GEN_PROGS_EXTENDED))
TEST_GEN_FILES := $(patsubst %,$(OUTPUT)/%,$(TEST_GEN_FILES))

all: $(TEST_GEN_PROGS) $(TEST_GEN_PROGS_EXTENDED) $(TEST_GEN_FILES)

.ONESHELL:
define RUN_TESTS
	@test_num=`echo 0`;
	@echo "TAP version 13";
	@for TEST in $(1); do				\
		BASENAME_TEST=`basename $$TEST`;	\
		test_num=`echo $$test_num+1 | bc`;	\
		echo "selftests: $$BASENAME_TEST";	\
		echo "========================================";	\
		if [ ! -x $$TEST ]; then	\
			echo "selftests: Warning: file $$BASENAME_TEST is not executable, correct this.";\
			echo "not ok 1..$$test_num selftests: $$BASENAME_TEST [FAIL]"; \
		else					\
			cd `dirname $$TEST` > /dev/null; (./$$BASENAME_TEST > /tmp/$$BASENAME_TEST 2>&1 && echo "ok 1..$$test_num selftests: $$BASENAME_TEST [PASS]") || echo "not ok 1..$$test_num selftests:  $$BASENAME_TEST [FAIL]"; cd - > /dev/null;\
		fi;					\
	done;
endef

run_tests: all
ifneq ($(KBUILD_SRC),)
	@if [ "X$(TEST_PROGS) $(TEST_PROGS_EXTENDED) $(TEST_FILES)" != "X" ]; then
		@rsync -aq $(TEST_PROGS) $(TEST_PROGS_EXTENDED) $(TEST_FILES) $(OUTPUT)
	fi
	@if [ "X$(TEST_PROGS)" != "X" ]; then
		$(call RUN_TESTS, $(TEST_GEN_PROGS) $(TEST_CUSTOM_PROGS) $(OUTPUT)/$(TEST_PROGS))
	else
		$(call RUN_TESTS, $(TEST_GEN_PROGS) $(TEST_CUSTOM_PROGS))
	fi
else
	$(call RUN_TESTS, $(TEST_GEN_PROGS) $(TEST_CUSTOM_PROGS) $(TEST_PROGS))
endif

define INSTALL_RULE
	@if [ "X$(TEST_PROGS)$(TEST_PROGS_EXTENDED)$(TEST_FILES)" != "X" ]; then					\
		mkdir -p ${INSTALL_PATH};										\
		echo "rsync -a $(TEST_PROGS) $(TEST_PROGS_EXTENDED) $(TEST_FILES) $(INSTALL_PATH)/";	\
		rsync -a $(TEST_PROGS) $(TEST_PROGS_EXTENDED) $(TEST_FILES) $(INSTALL_PATH)/;		\
	fi
	@if [ "X$(TEST_GEN_PROGS)$(TEST_CUSTOM_PROGS)$(TEST_GEN_PROGS_EXTENDED)$(TEST_GEN_FILES)" != "X" ]; then					\
		mkdir -p ${INSTALL_PATH};										\
		echo "rsync -a $(TEST_GEN_PROGS) $(TEST_CUSTOM_PROGS) $(TEST_GEN_PROGS_EXTENDED) $(TEST_GEN_FILES) $(INSTALL_PATH)/";	\
		rsync -a $(TEST_GEN_PROGS) $(TEST_CUSTOM_PROGS) $(TEST_GEN_PROGS_EXTENDED) $(TEST_GEN_FILES) $(INSTALL_PATH)/;		\
	fi
endef

install: all
ifdef INSTALL_PATH
	$(INSTALL_RULE)
else
	$(error Error: set INSTALL_PATH to use install)
endif

define EMIT_TESTS
	@for TEST in $(TEST_GEN_PROGS) $(TEST_CUSTOM_PROGS) $(TEST_PROGS); do \
		BASENAME_TEST=`basename $$TEST`;	\
		echo "(./$$BASENAME_TEST > /tmp/$$BASENAME_TEST 2>&1 && echo \"selftests: $$BASENAME_TEST [PASS]\") || echo \"selftests: $$BASENAME_TEST [FAIL]\""; \
	done;
endef

emit_tests:
	$(EMIT_TESTS)

# define if isn't already. It is undefined in make O= case.
ifeq ($(RM),)
RM := rm -f
endif

define CLEAN
	$(RM) -r $(TEST_GEN_PROGS) $(TEST_GEN_PROGS_EXTENDED) $(TEST_GEN_FILES) $(EXTRA_CLEAN)
endef

clean:
	$(CLEAN)

# When make O= with kselftest target from main level
# the following aren't defined.
#
ifneq ($(KBUILD_SRC),)
LINK.c = $(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) $(TARGET_ARCH)
COMPILE.S = $(CC) $(ASFLAGS) $(CPPFLAGS) $(TARGET_ARCH) -c
LINK.S = $(CC) $(ASFLAGS) $(CPPFLAGS) $(LDFLAGS) $(TARGET_ARCH)
endif

$(OUTPUT)/%:%.c
	$(LINK.c) $^ $(LDLIBS) -o $@

$(OUTPUT)/%.o:%.S
	$(COMPILE.S) $^ -o $@

$(OUTPUT)/%:%.S
	$(LINK.S) $^ $(LDLIBS) -o $@

.PHONY: run_tests all clean install emit_tests
