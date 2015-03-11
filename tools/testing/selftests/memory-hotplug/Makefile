all:

include ../lib.mk

override RUN_TESTS := ./on-off-test.sh -r 2 || echo "selftests: memory-hotplug [FAIL]"

run_full_test:
	@/bin/bash ./on-off-test.sh || echo "memory-hotplug selftests: [FAIL]"

clean:
