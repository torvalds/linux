#!/bin/bash

heapsize=4096
TCID="ion_test.sh"
errcode=0

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

run_test()
{
	heaptype=$1
	./ionapp_export -i $heaptype -s $heapsize &
	sleep 1
	./ionapp_import
	if [ $? -ne 0 ]; then
		echo "$TCID: heap_type: $heaptype - [FAIL]"
		errcode=1
	else
		echo "$TCID: heap_type: $heaptype - [PASS]"
	fi
	sleep 1
	echo ""
}

check_root()
{
	uid=$(id -u)
	if [ $uid -ne 0 ]; then
		echo $TCID: must be run as root >&2
		exit $ksft_skip
	fi
}

check_device()
{
	DEVICE=/dev/ion
	if [ ! -e $DEVICE ]; then
		echo $TCID: No $DEVICE device found >&2
		echo $TCID: May be CONFIG_ION is not set >&2
		exit $ksft_skip
	fi
}

main_function()
{
	check_device
	check_root

	# ION_SYSTEM_HEAP TEST
	run_test 0
	# ION_SYSTEM_CONTIG_HEAP TEST
	run_test 1
}

main_function
echo "$TCID: done"
exit $errcode
