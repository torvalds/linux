#!/bin/sh
# Checks fast/slow prime_number generation for inconsistencies

if ! /sbin/modprobe -q -r prime_numbers; then
	echo "prime_numbers: [SKIP]"
	exit 77
fi

if /sbin/modprobe -q prime_numbers selftest=65536; then
	/sbin/modprobe -q -r prime_numbers
	echo "prime_numbers: ok"
else
	echo "prime_numbers: [FAIL]"
	exit 1
fi
