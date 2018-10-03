#!/bin/sh
# Runs copy_to/from_user infrastructure using test_user_copy kernel module

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

if ! /sbin/modprobe -q -n test_user_copy; then
	echo "user: module test_user_copy is not found [SKIP]"
	exit $ksft_skip
fi
if /sbin/modprobe -q test_user_copy; then
	/sbin/modprobe -q -r test_user_copy
	echo "user_copy: ok"
else
	echo "user_copy: [FAIL]"
	exit 1
fi
