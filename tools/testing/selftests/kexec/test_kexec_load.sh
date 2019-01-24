#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Loading a kernel image via the kexec_load syscall should fail
# when the kernel is CONFIG_KEXEC_VERIFY_SIG enabled and the system
# is booted in secureboot mode.

TEST="$0"
. ./kexec_common_lib.sh
rc=0

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

# kexec requires root privileges
if [ $(id -ru) -ne 0 ]; then
	echo "$TEST: requires root privileges" >&2
	exit $ksft_skip
fi

get_secureboot_mode
secureboot=$?

# kexec_load should fail in secure boot mode
KERNEL_IMAGE="/boot/vmlinuz-`uname -r`"
kexec --load $KERNEL_IMAGE > /dev/null 2>&1
if [ $? -eq 0 ]; then
	kexec --unload
	if [ $secureboot -eq 1 ]; then
		echo "$TEST: kexec_load succeeded [FAIL]"
		rc=1
	else
		echo "$TEST: kexec_load succeeded [PASS]"
	fi
else
	if [ $secureboot -eq 1 ]; then
		echo "$TEST: kexec_load failed [PASS]"
	else
		echo "$TEST: kexec_load failed [FAIL]"
		rc=1
	fi
fi

exit $rc
