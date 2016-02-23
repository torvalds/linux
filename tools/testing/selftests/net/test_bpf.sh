#!/bin/sh
# Runs bpf test using test_bpf kernel module

if /sbin/modprobe -q test_bpf ; then
	/sbin/modprobe -q -r test_bpf;
	echo "test_bpf: ok";
else
	echo "test_bpf: [FAIL]";
	exit 1;
fi
