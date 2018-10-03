#!/bin/bash
TCID="zram.sh"

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

. ./zram_lib.sh

run_zram () {
echo "--------------------"
echo "running zram tests"
echo "--------------------"
./zram01.sh
echo ""
./zram02.sh
}

check_prereqs

# check zram module exists
MODULE_PATH=/lib/modules/`uname -r`/kernel/drivers/block/zram/zram.ko
if [ -f $MODULE_PATH ]; then
	run_zram
elif [ -b /dev/zram0 ]; then
	run_zram
else
	echo "$TCID : No zram.ko module or /dev/zram0 device file not found"
	echo "$TCID : CONFIG_ZRAM is not set"
	exit $ksft_skip
fi
