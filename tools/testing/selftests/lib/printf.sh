#!/bin/sh
# Runs printf infrastructure using test_printf kernel module
if ! /sbin/modprobe -q -n test_printf; then
	echo "printf: [SKIP]"
	exit 77
fi

if /sbin/modprobe -q test_printf; then
	/sbin/modprobe -q -r test_printf
	echo "printf: ok"
else
	echo "printf: [FAIL]"
	exit 1
fi
