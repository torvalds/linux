#!/bin/sh
# Runs jump label kernel module tests

if /sbin/modprobe -q test_jump_label_base; then
	if /sbin/modprobe -q test_jump_label; then
		echo "jump_label: ok"
		/sbin/modprobe -q -r test_jump_label
		/sbin/modprobe -q -r test_jump_label_base
	else
		echo "jump_label: [FAIL]"
		/sbin/modprobe -q -r test_jump_label_base
	fi
else
	echo "jump_label: [FAIL]"
	exit 1
fi
