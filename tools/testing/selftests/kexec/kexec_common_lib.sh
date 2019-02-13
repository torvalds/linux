#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Kselftest framework defines: ksft_pass=0, ksft_fail=1, ksft_skip=4

VERBOSE="${VERBOSE:-1}"

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
# The secure boot mode can be accessed either as the last integer
# of "od -An -t u1 /sys/firmware/efi/efivars/SecureBoot-*" or from
# "od -An -t u1 /sys/firmware/efi/vars/SecureBoot-*/data".  The efi
# SetupMode can be similarly accessed.
# Return 1 for SecureBoot mode enabled and SetupMode mode disabled.
get_secureboot_mode()
{
	local efivarfs="/sys/firmware/efi/efivars"
	local secure_boot_file="$efivarfs/../vars/SecureBoot-*/data"
	local setup_mode_file="$efivarfs/../vars/SetupMode-*/data"
	local secureboot_mode=0
	local setup_mode=0

	# Make sure that efivars is mounted in the normal location
	if ! grep -q "^\S\+ $efivarfs efivarfs" /proc/mounts; then
		log_skip "efivars is not mounted on $efivarfs"
	fi

	# Due to globbing, quoting "secure_boot_file" and "setup_mode_file"
	# is not possible.  (Todo: initialize variables using find or ls.)
	if [ ! -e $secure_boot_file ] || [ ! -e $setup_mode_file ]; then
		log_skip "unknown secureboot/setup mode"
	fi

	secureboot_mode=`od -An -t u1 $secure_boot_file`
	setup_mode=`od -An -t u1 $setup_mode_file`

	if [ $secureboot_mode -eq 1 ] && [ $setup_mode -eq 0 ]; then
		log_info "secure boot mode enabled"
		return 1;
	fi
	log_info "secure boot mode not enabled"
	return 0;
}
