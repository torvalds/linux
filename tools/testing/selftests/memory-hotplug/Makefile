all:

include ../lib.mk

TEST_PROGS := mem-on-off-test.sh
override RUN_TESTS := ./mem-on-off-test.sh -r 2 || echo "selftests: memory-hotplug [FAIL]"
override EMIT_TESTS := echo "$(RUN_TESTS)"

run_full_test:
	@/bin/bash ./mem-on-off-test.sh || echo "memory-hotplug selftests: [FAIL]"

clean:
