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

kconfig_enabled "CONFIG_KEXEC=y" "kexec_load is enabled"
if [ $? -eq 0 ]; then
	log_skip "kexec_load is not enabled"
fi

kconfig_enabled "CONFIG_IMA_APPRAISE=y" "IMA enabled"
ima_appraise=$?

kconfig_enabled "CONFIG_IMA_ARCH_POLICY=y" \
	"IMA architecture specific policy enabled"
arch_policy=$?

get_secureboot_mode
secureboot=$?

# kexec_load should fail in secure boot mode and CONFIG_IMA_ARCH_POLICY enabled
kexec --load $KERNEL_IMAGE > /dev/null 2>&1
if [ $? -eq 0 ]; then
	kexec --unload
	if [ $secureboot -eq 1 ] && [ $arch_policy -eq 1 ]; then
		log_fail "kexec_load succeeded"
	elif [ $ima_appraise -eq 0 -o $arch_policy -eq 0 ]; then
		log_info "Either IMA or the IMA arch policy is not enabled"
	fi
	log_pass "kexec_load succeeded"
else
	if [ $secureboot -eq 1 ] && [ $arch_policy -eq 1 ] ; then
		log_pass "kexec_load failed"
	else
		log_fail "kexec_load failed"
	fi
fi
