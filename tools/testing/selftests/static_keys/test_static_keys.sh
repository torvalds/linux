#!/bin/sh
# Runs static keys kernel module tests

if /sbin/modprobe -q test_static_key_base; then
	if /sbin/modprobe -q test_static_keys; then
		echo "static_key: ok"
		/sbin/modprobe -q -r test_static_keys
		/sbin/modprobe -q -r test_static_key_base
	else
		echo "static_keys: [FAIL]"
		/sbin/modprobe -q -r test_static_key_base
	fi
else
	echo "static_key: [FAIL]"
	exit 1
fi
