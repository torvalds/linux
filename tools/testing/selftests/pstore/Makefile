# Makefile for pstore selftests.
# Expects pstore backend is registered.

all:

TEST_PROGS := pstore_tests pstore_post_reboot_tests
TEST_FILES := common_tests pstore_crash_test

include ../lib.mk

run_crash:
	@sh pstore_crash_test || { echo "pstore_crash_test: [FAIL]"; exit 1; }

clean:
	rm -rf logs/* *uuid
