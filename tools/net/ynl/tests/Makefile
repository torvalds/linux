# SPDX-License-Identifier: GPL-2.0
# Makefile for YNL tests

TEST_PROGS := \
	test_ynl_cli.sh \
	test_ynl_ethtool.sh \
# end of TEST_PROGS

all: $(TEST_PROGS)

run_tests:
	@for test in $(TEST_PROGS); do \
		./$$test; \
	done

install: $(TEST_PROGS)
	@mkdir -p $(DESTDIR)/usr/bin
	@mkdir -p $(DESTDIR)/usr/share/kselftest
	@cp ../../../testing/selftests/kselftest/ktap_helpers.sh $(DESTDIR)/usr/share/kselftest/
	@for test in $(TEST_PROGS); do \
		name=$$(basename $$test .sh); \
		sed -e 's|^ynl=.*|ynl="ynl"|' \
		    -e 's|^ynl_ethtool=.*|ynl_ethtool="ynl-ethtool"|' \
		    -e 's|KSELFTEST_KTAP_HELPERS=.*|KSELFTEST_KTAP_HELPERS="/usr/share/kselftest/ktap_helpers.sh"|' \
		    $$test > $(DESTDIR)/usr/bin/$$name; \
		chmod +x $(DESTDIR)/usr/bin/$$name; \
	done

clean distclean:
	@# Nothing to clean

.PHONY: all install clean run_tests
