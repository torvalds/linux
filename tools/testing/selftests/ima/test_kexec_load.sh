#!/bin/sh
# SPDX-License-Identifier: GPL-2.0+
# Loading a kernel image via the kexec_load syscall should fail
# when the kerne is CONFIG_KEXEC_VERIFY_SIG enabled and the system
# is booted in secureboot mode.

TEST="$0"
EFIVARFS="/sys/firmware/efi/efivars"
rc=0

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

# kexec requires root privileges
if [ $UID != 0 ]; then
	echo "$TEST: must be run as root" >&2
	exit $ksft_skip
fi

# Make sure that efivars is mounted in the normal location
if ! grep -q "^\S\+ $EFIVARFS efivarfs" /proc/mounts; then
	echo "$TEST: efivars is not mounted on $EFIVARFS" >&2
	exit $ksft_skip
fi

# Get secureboot mode
file="$EFIVARFS/SecureBoot-*"
if [ ! -e $file ]; then
	echo "$TEST: unknown secureboot mode" >&2
	exit $ksft_skip
fi
secureboot=`hexdump $file | awk '{print substr($4,length($4),1)}'`

# kexec_load should fail in secure boot mode
KERNEL_IMAGE="/boot/vmlinuz-`uname -r`"
kexec -l $KERNEL_IMAGE &>> /dev/null
if [ $? == 0 ]; then
	kexec -u
	if [ "$secureboot" == "1" ]; then
		echo "$TEST: kexec_load succeeded [FAIL]"
		rc=1
	else
		echo "$TEST: kexec_load succeeded [PASS]"
	fi
else
	if [ "$secureboot" == "1" ]; then
		echo "$TEST: kexec_load failed [PASS]"
	else
		echo "$TEST: kexec_load failed [FAIL]"
		rc=1
	fi
fi

exit $rc
