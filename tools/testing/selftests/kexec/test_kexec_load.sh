#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Loading a kernel image via the kexec_load syscall should fail
# when the kernel is CONFIG_KEXEC_VERIFY_SIG enabled and the system
# is booted in secureboot mode.

TEST="$0"
. ./kexec_common_lib.sh

# kexec requires root privileges
require_root_privileges

get_secureboot_mode
secureboot=$?

# kexec_load should fail in secure boot mode
KERNEL_IMAGE="/boot/vmlinuz-`uname -r`"
kexec --load $KERNEL_IMAGE > /dev/null 2>&1
if [ $? -eq 0 ]; then
	kexec --unload
	if [ $secureboot -eq 1 ]; then
		log_fail "kexec_load succeeded"
	else
		log_pass "kexec_load succeeded"
	fi
else
	if [ $secureboot -eq 1 ]; then
		log_pass "kexec_load failed"
	else
		log_fail "kexec_load failed"
	fi
fi
