#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Prevent loading a kernel image via the kexec_load syscall when
# signatures are required.  (Dependent on CONFIG_IMA_ARCH_POLICY.)

TEST="$0"
. ./kexec_common_lib.sh

# kexec requires root privileges
require_root_privileges

# get the kernel config
get_kconfig

kconfig_enabled "CONFIG_KEXEC_JUMP=y" "kexec_jump is enabled"
if [ $? -eq 0 ]; then
	log_skip "kexec_jump is not enabled"
fi

kconfig_enabled "CONFIG_IMA_APPRAISE=y" "IMA enabled"
ima_appraise=$?

kconfig_enabled "CONFIG_IMA_ARCH_POLICY=y" \
	"IMA architecture specific policy enabled"
arch_policy=$?

get_secureboot_mode
secureboot=$?

if [ $secureboot -eq 1 ] && [ $arch_policy -eq 1 ]; then
    log_skip "Secure boot and CONFIG_IMA_ARCH_POLICY are enabled"
fi

./test_kexec_jump
if [ $? -eq 0 ]; then
    log_pass "kexec_jump succeeded"
else
    # The more likely failure mode if anything went wrong is that the
    # kernel just crashes. But if we get back here, sure, whine anyway.
    log_fail "kexec_jump failed"
fi
