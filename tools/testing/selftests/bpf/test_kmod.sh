#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

SRC_TREE=../../../../

test_run()
{
	sysctl -w net.core.bpf_jit_enable=$1 2>&1 > /dev/null
	sysctl -w net.core.bpf_jit_harden=$2 2>&1 > /dev/null

	echo "[ JIT enabled:$1 hardened:$2 ]"
	dmesg -C
	if [ -f ${SRC_TREE}/lib/test_bpf.ko ]; then
		insmod ${SRC_TREE}/lib/test_bpf.ko 2> /dev/null
		if [ $? -ne 0 ]; then
			rc=1
		fi
	else
		# Use modprobe dry run to check for missing test_bpf module
		if ! /sbin/modprobe -q -n test_bpf; then
			echo "test_bpf: [SKIP]"
		elif /sbin/modprobe -q test_bpf; then
			echo "test_bpf: ok"
		else
			echo "test_bpf: [FAIL]"
			rc=1
		fi
	fi
	rmmod  test_bpf 2> /dev/null
	dmesg | grep FAIL
}

test_save()
{
	JE=`sysctl -n net.core.bpf_jit_enable`
	JH=`sysctl -n net.core.bpf_jit_harden`
}

test_restore()
{
	sysctl -w net.core.bpf_jit_enable=$JE 2>&1 > /dev/null
	sysctl -w net.core.bpf_jit_harden=$JH 2>&1 > /dev/null
}

rc=0
test_save
test_run 0 0
test_run 1 0
test_run 1 1
test_run 1 2
test_restore
exit $rc
