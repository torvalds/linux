all:

TEST_PROGS := zram.sh
TEST_FILES := zram01.sh zram02.sh zram_lib.sh

include ../lib.mk

run_tests:
	@/bin/bash ./zram.sh

clean:
	$(RM) err.log
