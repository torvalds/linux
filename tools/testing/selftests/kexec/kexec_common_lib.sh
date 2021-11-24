#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Kselftest framework defines: ksft_pass=0, ksft_fail=1, ksft_skip=4

VERBOSE="${VERBOSE:-1}"
IKCONFIG="/tmp/config-`uname -r`"
KERNEL_IMAGE="/boot/vmlinuz-`uname -r`"
SECURITYFS=$(grep "securityfs" /proc/mounts | awk '{print $2}')

log_info()
{
	[ $VERBOSE -ne 0 ] && echo "[INFO] $1"
}

# The ksefltest framework requirement returns 0 for PASS.
log_pass()
{
	[ $VERBOSE -ne 0 ] && echo "$1 [PASS]"
	exit 0
}

# The ksefltest framework requirement returns 1 for FAIL.
log_fail()
{
	[ $VERBOSE -ne 0 ] && echo "$1 [FAIL]"
	exit 1
}

# The ksefltest framework requirement returns 4 for SKIP.
log_skip()
{
	[ $VERBOSE -ne 0 ] && echo "$1"
	exit 4
}

# Check efivar SecureBoot-$(the UUID) and SetupMode-$(the UUID).
# (Based on kdump-lib.sh)
get_efivarfs_secureboot_mode()
{
	local efivarfs="/sys/firmware/efi/efivars"
	local secure_boot_file=""
	local setup_mode_file=""
	local secureboot_mode=0
	local setup_mode=0

	# Make sure that efivar_fs is mounted in the normal location
	if ! grep -q "^\S\+ $efivarfs efivarfs" /proc/mounts; then
		log_info "efivars is not mounted on $efivarfs"
		return 0;
	fi
	secure_boot_file=$(find "$efivarfs" -name SecureBoot-* 2>/dev/null)
	setup_mode_file=$(find "$efivarfs" -name SetupMode-* 2>/dev/null)
	if [ -f "$secure_boot_file" ] && [ -f "$setup_mode_file" ]; then
		secureboot_mode=$(hexdump -v -e '/1 "%d\ "' \
			"$secure_boot_file"|cut -d' ' -f 5)
		setup_mode=$(hexdump -v -e '/1 "%d\ "' \
			"$setup_mode_file"|cut -d' ' -f 5)

		if [ $secureboot_mode -eq 1 ] && [ $setup_mode -eq 0 ]; then
			log_info "secure boot mode enabled (CONFIG_EFIVAR_FS)"
			return 1;
		fi
	fi
	return 0;
}

get_efi_var_secureboot_mode()
{
	local efi_vars
	local secure_boot_file
	local setup_mode_file
	local secureboot_mode
	local setup_mode

	if [ ! -d "$efi_vars" ]; then
		log_skip "efi_vars is not enabled\n"
	fi
	secure_boot_file=$(find "$efi_vars" -name SecureBoot-* 2>/dev/null)
	setup_mode_file=$(find "$efi_vars" -name SetupMode-* 2>/dev/null)
	if [ -f "$secure_boot_file/data" ] && \
	   [ -f "$setup_mode_file/data" ]; then
		secureboot_mode=`od -An -t u1 "$secure_boot_file/data"`
		setup_mode=`od -An -t u1 "$setup_mode_file/data"`

		if [ $secureboot_mode -eq 1 ] && [ $setup_mode -eq 0 ]; then
			log_info "secure boot mode enabled (CONFIG_EFI_VARS)"
			return 1;
		fi
	fi
	return 0;
}

# On powerpc platform, check device-tree property
# /proc/device-tree/ibm,secureboot/os-secureboot-enforcing
# to detect secureboot state.
get_ppc64_secureboot_mode()
{
	local secure_boot_file="/proc/device-tree/ibm,secureboot/os-secureboot-enforcing"
	# Check for secure boot file existence
	if [ -f $secure_boot_file ]; then
		log_info "Secureboot is enabled (Device tree)"
		return 1;
	fi
	log_info "Secureboot is not enabled (Device tree)"
	return 0;
}

# Return the architecture of the system
get_arch()
{
	echo $(arch)
}

# Check efivar SecureBoot-$(the UUID) and SetupMode-$(the UUID).
# The secure boot mode can be accessed either as the last integer
# of "od -An -t u1 /sys/firmware/efi/efivars/SecureBoot-*" or from
# "od -An -t u1 /sys/firmware/efi/vars/SecureBoot-*/data".  The efi
# SetupMode can be similarly accessed.
# Return 1 for SecureBoot mode enabled and SetupMode mode disabled.
get_secureboot_mode()
{
	local secureboot_mode=0
	local system_arch=$(get_arch)

	if [ "$system_arch" == "ppc64le" ]; then
		get_ppc64_secureboot_mode
		secureboot_mode=$?
	else
		get_efivarfs_secureboot_mode
		secureboot_mode=$?
		# fallback to using the efi_var files
		if [ $secureboot_mode -eq 0 ]; then
			get_efi_var_secureboot_mode
			secureboot_mode=$?
		fi
	fi

	if [ $secureboot_mode -eq 0 ]; then
		log_info "secure boot mode not enabled"
	fi
	return $secureboot_mode;
}

require_root_privileges()
{
	if [ $(id -ru) -ne 0 ]; then
		log_skip "requires root privileges"
	fi
}

# Look for config option in Kconfig file.
# Return 1 for found and 0 for not found.
kconfig_enabled()
{
	local config="$1"
	local msg="$2"

	grep -E -q $config $IKCONFIG
	if [ $? -eq 0 ]; then
		log_info "$msg"
		return 1
	fi
	return 0
}

# Attempt to get the kernel config first by checking the modules directory
# then via proc, and finally by extracting it from the kernel image or the
# configs.ko using scripts/extract-ikconfig.
# Return 1 for found.
get_kconfig()
{
	local proc_config="/proc/config.gz"
	local module_dir="/lib/modules/`uname -r`"
	local configs_module="$module_dir/kernel/kernel/configs.ko*"

	if [ -f $module_dir/config ]; then
		IKCONFIG=$module_dir/config
		return 1
	fi

	if [ ! -f $proc_config ]; then
		modprobe configs > /dev/null 2>&1
	fi
	if [ -f $proc_config ]; then
		cat $proc_config | gunzip > $IKCONFIG 2>/dev/null
		if [ $? -eq 0 ]; then
			return 1
		fi
	fi

	local extract_ikconfig="$module_dir/source/scripts/extract-ikconfig"
	if [ ! -f $extract_ikconfig ]; then
		log_skip "extract-ikconfig not found"
	fi

	$extract_ikconfig $KERNEL_IMAGE > $IKCONFIG 2>/dev/null
	if [ $? -eq 1 ]; then
		if [ ! -f $configs_module ]; then
			log_skip "CONFIG_IKCONFIG not enabled"
		fi
		$extract_ikconfig $configs_module > $IKCONFIG
		if [ $? -eq 1 ]; then
			log_skip "CONFIG_IKCONFIG not enabled"
		fi
	fi
	return 1
}

# Make sure that securityfs is mounted
mount_securityfs()
{
	if [ -z $SECURITYFS ]; then
		SECURITYFS=/sys/kernel/security
		mount -t securityfs security $SECURITYFS
	fi

	if [ ! -d "$SECURITYFS" ]; then
		log_fail "$SECURITYFS :securityfs is not mounted"
	fi
}

# The policy rule format is an "action" followed by key-value pairs.  This
# function supports up to two key-value pairs, in any order.
# For example: action func=<keyword> [appraise_type=<type>]
# Return 1 for found and 0 for not found.
check_ima_policy()
{
	local action="$1"
	local keypair1="$2"
	local keypair2="$3"
	local ret=0

	mount_securityfs

	local ima_policy=$SECURITYFS/ima/policy
	if [ ! -e $ima_policy ]; then
		log_fail "$ima_policy not found"
	fi

	if [ -n $keypair2 ]; then
		grep -e "^$action.*$keypair1" "$ima_policy" | \
			grep -q -e "$keypair2"
	else
		grep -q -e "^$action.*$keypair1" "$ima_policy"
	fi

	# invert "grep -q" result, returning 1 for found.
	[ $? -eq 0 ] && ret=1
	return $ret
}
