# This mimics the top-level Makefile. We do it explicitly here so that this
# Makefile can operate with or without the kbuild infrastructure.
CC := $(CROSS_COMPILE)gcc

define RUN_TESTS
	@for TEST in $(TEST_PROGS); do \
		(./$$TEST && echo "selftests: $$TEST [PASS]") || echo "selftests: $$TEST [FAIL]"; \
	done;
endef

run_tests: all
	$(RUN_TESTS)

define INSTALL_RULE
	mkdir -p $(INSTALL_PATH)
	@for TEST_DIR in $(TEST_DIRS); do\
		cp -r $$TEST_DIR $(INSTALL_PATH); \
	done;
	install -t $(INSTALL_PATH) $(TEST_PROGS) $(TEST_PROGS_EXTENDED) $(TEST_FILES)
endef

install: all
ifdef INSTALL_PATH
	$(INSTALL_RULE)
else
	$(error Error: set INSTALL_PATH to use install)
endif

define EMIT_TESTS
	@for TEST in $(TEST_PROGS); do \
		echo "(./$$TEST && echo \"selftests: $$TEST [PASS]\") || echo \"selftests: $$TEST [FAIL]\""; \
	done;
endef

emit_tests:
	$(EMIT_TESTS)

.PHONY: run_tests all clean install emit_tests
