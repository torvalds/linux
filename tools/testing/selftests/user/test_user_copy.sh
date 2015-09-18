#!/bin/sh
# Runs copy_to/from_user infrastructure using test_user_copy kernel module

if /sbin/modprobe -q test_user_copy; then
	/sbin/modprobe -q -r test_user_copy
	echo "user_copy: ok"
else
	echo "user_copy: [FAIL]"
	exit 1
fi
