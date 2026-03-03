# SPDX-License-Identifier: GPL-2.0
# Makefile for YNL tests

TEST_PROGS := \
	test_ynl_cli.sh \
	test_ynl_ethtool.sh \
# end of TEST_PROGS

INSTALL_PATH ?= $(DESTDIR)/usr/share/kselftest

all: $(TEST_PROGS)

run_tests:
	@for test in $(TEST_PROGS); do \
		./$$test; \
	done

install: $(TEST_PROGS)
	@mkdir -p $(INSTALL_PATH)/ynl
	@cp ../../../testing/selftests/kselftest/ktap_helpers.sh $(INSTALL_PATH)/
	@for test in $(TEST_PROGS); do \
		name=$$(basename $$test); \
		sed -e 's|^ynl=.*|ynl="ynl"|' \
		    -e 's|^ynl_ethtool=.*|ynl_ethtool="ynl-ethtool"|' \
		    -e 's|KSELFTEST_KTAP_HELPERS=.*|KSELFTEST_KTAP_HELPERS="$(INSTALL_PATH)/ktap_helpers.sh"|' \
		    $$test > $(INSTALL_PATH)/ynl/$$name; \
		chmod +x $(INSTALL_PATH)/ynl/$$name; \
	done
	@for test in $(TEST_PROGS); do \
		echo "ynl:$$test"; \
	done > $(INSTALL_PATH)/kselftest-list.txt

clean distclean:
	@# Nothing to clean

.PHONY: all install clean run_tests
