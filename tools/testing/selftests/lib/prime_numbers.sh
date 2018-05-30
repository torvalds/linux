#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Checks fast/slow prime_number generation for inconsistencies

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

if ! /sbin/modprobe -q -n prime_numbers; then
	echo "prime_numbers: module prime_numbers is not found [SKIP]"
	exit $ksft_skip
fi

if /sbin/modprobe -q prime_numbers selftest=65536; then
	/sbin/modprobe -q -r prime_numbers
	echo "prime_numbers: ok"
else
	echo "prime_numbers: [FAIL]"
	exit 1
fi
