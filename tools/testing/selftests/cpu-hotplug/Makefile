all:

TEST_PROGS := cpu-on-off-test.sh

include ../lib.mk

run_full_test:
	@/bin/bash ./cpu-on-off-test.sh -a || echo "cpu-hotplug selftests: [FAIL]"

clean:
