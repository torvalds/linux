# This mimics the top-level Makefile. We do it explicitly here so that this
# Makefile can operate with or without the kbuild infrastructure.
CC := $(CROSS_COMPILE)gcc

TEST_GEN_PROGS := $(patsubst %,$(OUTPUT)/%,$(TEST_GEN_PROGS))
TEST_GEN_FILES := $(patsubst %,$(OUTPUT)/%,$(TEST_GEN_FILES))

all: $(TEST_GEN_PROGS) $(TEST_GEN_PROGS_EXTENDED) $(TEST_GEN_FILES)

define RUN_TESTS
	@for TEST in $(TEST_GEN_PROGS) $(TEST_PROGS); do \
		BASENAME_TEST=`basename $$TEST`;	\
		cd `dirname $$TEST`; (./$$BASENAME_TEST && echo "selftests: $$BASENAME_TEST [PASS]") || echo "selftests:  $$BASENAME_TEST [FAIL]"; cd -;\
	done;
endef

run_tests: all
	$(RUN_TESTS)

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

clean:
	$(RM) -r $(TEST_GEN_PROGS) $(TEST_GEN_PROGS_EXTENDED) $(TEST_GEN_FILES) $(EXTRA_CLEAN)

$(OUTPUT)/%:%.c
	$(CC) $(CFLAGS) $(LDFLAGS) $(LDLIBS) $< -o $@

$(OUTPUT)/%.o:%.S
	$(CC) $(ASFLAGS) -c $< -o $@

$(OUTPUT)/%:%.S
	$(CC) $(ASFLAGS) $< -o $@

.PHONY: run_tests all clean install emit_tests
