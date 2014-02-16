# Makefile for user memory selftests

# No binaries, but make sure arg-less "make" doesn't trigger "run_tests"
all:

run_tests: all
	@if /sbin/modprobe test_user_copy ; then \
		rmmod test_user_copy; \
		echo "user_copy: ok"; \
	else \
		echo "user_copy: [FAIL]"; \
		exit 1; \
	fi
