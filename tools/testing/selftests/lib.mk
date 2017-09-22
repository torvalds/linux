# This mimics the top-level Makefile. We do it explicitly here so that this
# Makefile can operate with or without the kbuild infrastructure.
CC := $(CROSS_COMPILE)gcc

ifeq (0,$(MAKELEVEL))
OUTPUT := $(shell pwd)
endif

TEST_GEN_PROGS := $(patsubst %,$(OUTPUT)/%,$(TEST_GEN_PROGS))
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
		if [ ! -x $$BASENAME_TEST ]; then	\
			echo "selftests: Warning: file $$BASENAME_TEST is not executable, correct this.";\
			echo "not ok 1..$$test_num selftests: $$BASENAME_TEST [FAIL]"; \
		else					\
			cd `dirname $$TEST` > /dev/null; (./$$BASENAME_TEST && echo "ok 1..$$test_num selftests: $$BASENAME_TEST [PASS]") || echo "not ok 1..$$test_num selftests:  $$BASENAME_TEST [FAIL]"; cd - > /dev/null;\
		fi;					\
	done;
endef

run_tests: all
	$(call RUN_TESTS, $(TEST_GEN_PROGS) $(TEST_PROGS))

define INSTALL_RULE
	@if [ "X$(TEST_PROGS)$(TEST_PROGS_EXTENDED)$(TEST_FILES)" != "X" ]; then					\
		mkdir -p ${INSTALL_PATH};										\
		echo "rsync -a $(TEST_PROGS) $(TEST_PROGS_EXTENDED) $(TEST_FILES) $(INSTALL_PATH)/";	\
		rsync -a $(TEST_PROGS) $(TEST_PROGS_EXTENDED) $(TEST_FILES) $(INSTALL_PATH)/;		\
	fi
	@if [ "X$(TEST_GEN_PROGS)$(TEST_GEN_PROGS_EXTENDED)$(TEST_GEN_FILES)" != "X" ]; then					\
		mkdir -p ${INSTALL_PATH};										\
		echo "rsync -a $(TEST_GEN_PROGS) $(TEST_GEN_PROGS_EXTENDED) $(TEST_GEN_FILES) $(INSTALL_PATH)/";	\
		rsync -a $(TEST_GEN_PROGS) $(TEST_GEN_PROGS_EXTENDED) $(TEST_GEN_FILES) $(INSTALL_PATH)/;		\
	fi
endef

install: all
ifdef INSTALL_PATH
	$(INSTALL_RULE)
else
	$(error Error: set INSTALL_PATH to use install)
endif

define EMIT_TESTS
	@for TEST in $(TEST_GEN_PROGS) $(TEST_PROGS); do \
		BASENAME_TEST=`basename $$TEST`;	\
		echo "(./$$BASENAME_TEST && echo \"selftests: $$BASENAME_TEST [PASS]\") || echo \"selftests: $$BASENAME_TEST [FAIL]\""; \
	done;
endef

emit_tests:
	$(EMIT_TESTS)

define CLEAN
	$(RM) -r $(TEST_GEN_PROGS) $(TEST_GEN_PROGS_EXTENDED) $(TEST_GEN_FILES) $(EXTRA_CLEAN)
endef

clean:
	$(CLEAN)

$(OUTPUT)/%:%.c
	$(LINK.c) $^ $(LDLIBS) -o $@

$(OUTPUT)/%.o:%.S
	$(COMPILE.S) $^ -o $@

$(OUTPUT)/%:%.S
	$(LINK.S) $^ $(LDLIBS) -o $@

.PHONY: run_tests all clean install emit_tests
