#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

mod_name="revocable_test"
ksft_fail=1
ksft_skip=4

if [ "$(id -u)" -ne 0 ]; then
	echo "$0: Must be run as root"
	exit "$ksft_skip"
fi

if ! which insmod > /dev/null 2>&1; then
	echo "$0: Need insmod"
	exit "$ksft_skip"
fi

if ! which rmmod > /dev/null 2>&1; then
	echo "$0: Need rmmod"
	exit "$ksft_skip"
fi

insmod test_modules/"${mod_name}".ko

if [ ! -d /sys/kernel/debug/revocable_test/ ]; then
	mount -t debugfs none /sys/kernel/debug/

	if [ ! -d /sys/kernel/debug/revocable_test/ ]; then
		echo "$0: Error mounting debugfs"
		exit "$ksft_fail"
	fi
fi

./revocable_test
ret=$?

rmmod "${mod_name}"

exit "${ret}"
