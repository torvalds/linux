#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Library of helpers for test scripts.
set -e

DIR=/sys/devices/virtual/misc/test_firmware

PROC_CONFIG="/proc/config.gz"
TEST_DIR=$(dirname $0)

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

print_reqs_exit()
{
	echo "You must have the following enabled in your kernel:" >&2
	cat $TEST_DIR/config >&2
	exit $ksft_skip
}

test_modprobe()
{
	if [ ! -d $DIR ]; then
		print_reqs_exit
	fi
}

check_mods()
{
	trap "test_modprobe" EXIT
	if [ ! -d $DIR ]; then
		modprobe test_firmware
	fi
	if [ ! -f $PROC_CONFIG ]; then
		if modprobe configs 2>/dev/null; then
			echo "Loaded configs module"
			if [ ! -f $PROC_CONFIG ]; then
				echo "You must have the following enabled in your kernel:" >&2
				cat $TEST_DIR/config >&2
				echo "Resorting to old heuristics" >&2
			fi
		else
			echo "Failed to load configs module, using old heuristics" >&2
		fi
	fi
}

check_setup()
{
	HAS_FW_LOADER_USER_HELPER="$(kconfig_has CONFIG_FW_LOADER_USER_HELPER=y)"
	HAS_FW_LOADER_USER_HELPER_FALLBACK="$(kconfig_has CONFIG_FW_LOADER_USER_HELPER_FALLBACK=y)"
	HAS_FW_LOADER_COMPRESS="$(kconfig_has CONFIG_FW_LOADER_COMPRESS=y)"
	PROC_FW_IGNORE_SYSFS_FALLBACK="0"
	PROC_FW_FORCE_SYSFS_FALLBACK="0"

	if [ -z $PROC_SYS_DIR ]; then
		PROC_SYS_DIR="/proc/sys/kernel"
	fi

	FW_PROC="${PROC_SYS_DIR}/firmware_config"
	FW_FORCE_SYSFS_FALLBACK="$FW_PROC/force_sysfs_fallback"
	FW_IGNORE_SYSFS_FALLBACK="$FW_PROC/ignore_sysfs_fallback"

	if [ -f $FW_FORCE_SYSFS_FALLBACK ]; then
		PROC_FW_FORCE_SYSFS_FALLBACK="$(cat $FW_FORCE_SYSFS_FALLBACK)"
	fi

	if [ -f $FW_IGNORE_SYSFS_FALLBACK ]; then
		PROC_FW_IGNORE_SYSFS_FALLBACK="$(cat $FW_IGNORE_SYSFS_FALLBACK)"
	fi

	if [ "$PROC_FW_FORCE_SYSFS_FALLBACK" = "1" ]; then
		HAS_FW_LOADER_USER_HELPER="yes"
		HAS_FW_LOADER_USER_HELPER_FALLBACK="yes"
	fi

	if [ "$PROC_FW_IGNORE_SYSFS_FALLBACK" = "1" ]; then
		HAS_FW_LOADER_USER_HELPER_FALLBACK="no"
		HAS_FW_LOADER_USER_HELPER="no"
	fi

	if [ "$HAS_FW_LOADER_USER_HELPER" = "yes" ]; then
	       OLD_TIMEOUT="$(cat /sys/class/firmware/timeout)"
	fi

	OLD_FWPATH="$(cat /sys/module/firmware_class/parameters/path)"

	if [ "$HAS_FW_LOADER_COMPRESS" = "yes" ]; then
		if ! which xz 2> /dev/null > /dev/null; then
			HAS_FW_LOADER_COMPRESS=""
		fi
	fi
}

verify_reqs()
{
	if [ "$TEST_REQS_FW_SYSFS_FALLBACK" = "yes" ]; then
		if [ ! "$HAS_FW_LOADER_USER_HELPER" = "yes" ]; then
			echo "usermode helper disabled so ignoring test"
			exit 0
		fi
	fi
}

setup_tmp_file()
{
	FWPATH=$(mktemp -d)
	FW="$FWPATH/test-firmware.bin"
	echo "ABCD0123" >"$FW"
	NAME=$(basename "$FW")
	if [ "$TEST_REQS_FW_SET_CUSTOM_PATH" = "yes" ]; then
		echo -n "$FWPATH" >/sys/module/firmware_class/parameters/path
	fi
}

__setup_random_file()
{
	RANDOM_FILE_PATH="$(mktemp -p $FWPATH)"
	# mktemp says dry-run -n is unsafe, so...
	if [[ "$1" = "fake" ]]; then
		rm -rf $RANDOM_FILE_PATH
		sync
	else
		echo "ABCD0123" >"$RANDOM_FILE_PATH"
	fi
	echo $RANDOM_FILE_PATH
}

setup_random_file()
{
	echo $(__setup_random_file)
}

setup_random_file_fake()
{
	echo $(__setup_random_file fake)
}

proc_set_force_sysfs_fallback()
{
	if [ -f $FW_FORCE_SYSFS_FALLBACK ]; then
		echo -n $1 > $FW_FORCE_SYSFS_FALLBACK
		check_setup
	fi
}

proc_set_ignore_sysfs_fallback()
{
	if [ -f $FW_IGNORE_SYSFS_FALLBACK ]; then
		echo -n $1 > $FW_IGNORE_SYSFS_FALLBACK
		check_setup
	fi
}

proc_restore_defaults()
{
	proc_set_force_sysfs_fallback 0
	proc_set_ignore_sysfs_fallback 0
}

test_finish()
{
	if [ "$HAS_FW_LOADER_USER_HELPER" = "yes" ]; then
		echo "$OLD_TIMEOUT" >/sys/class/firmware/timeout
	fi
	if [ "$TEST_REQS_FW_SET_CUSTOM_PATH" = "yes" ]; then
		if [ "$OLD_FWPATH" = "" ]; then
			# A zero-length write won't work; write a null byte
			printf '\000' >/sys/module/firmware_class/parameters/path
		else
			echo -n "$OLD_FWPATH" >/sys/module/firmware_class/parameters/path
		fi
	fi
	if [ -f $FW ]; then
		rm -f "$FW"
	fi
	if [ -d $FWPATH ]; then
		rm -rf "$FWPATH"
	fi
	proc_restore_defaults
}

kconfig_has()
{
	if [ -f $PROC_CONFIG ]; then
		if zgrep -q $1 $PROC_CONFIG 2>/dev/null; then
			echo "yes"
		else
			echo "no"
		fi
	else
		# We currently don't have easy heuristics to infer this
		# so best we can do is just try to use the kernel assuming
		# you had enabled it. This matches the old behaviour.
		if [ "$1" = "CONFIG_FW_LOADER_USER_HELPER_FALLBACK=y" ]; then
			echo "yes"
		elif [ "$1" = "CONFIG_FW_LOADER_USER_HELPER=y" ]; then
			if [ -d /sys/class/firmware/ ]; then
				echo yes
			else
				echo no
			fi
		fi
	fi
}
