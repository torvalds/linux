#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

SRC_TREE=../../../../

test_run()
{
	if [ -f ${SRC_TREE}/drivers/char/adi.ko ]; then
		insmod ${SRC_TREE}/drivers/char/adi.ko 2> /dev/null
		if [ $? -ne 0 ]; then
			rc=1
		fi
	else
		# Use modprobe dry run to check for missing adi module
		if ! /sbin/modprobe -q -n adi; then
			echo "adi: [SKIP]"
		elif /sbin/modprobe -q adi; then
			echo "adi: ok"
		else
			echo "adi: [FAIL]"
			rc=1
		fi
	fi
	./adi-test
	rmmod adi 2> /dev/null
}

rc=0
test_run
exit $rc
