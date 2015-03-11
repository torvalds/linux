all:

TEST_PROGS := on-off-test.sh

include ../lib.mk

run_full_test:
	@/bin/bash ./on-off-test.sh -a || echo "cpu-hotplug selftests: [FAIL]"

clean:
