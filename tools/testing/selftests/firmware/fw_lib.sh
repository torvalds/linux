#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Library of helpers for test scripts.
set -e

DIR=/sys/devices/virtual/misc/test_firmware

PROC_CONFIG="/proc/config.gz"
TEST_DIR=$(dirname $0)

print_reqs_exit()
{
	echo "You must have the following enabled in your kernel:" >&2
	cat $TEST_DIR/config >&2
	exit 1
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

	if [ "$HAS_FW_LOADER_USER_HELPER" = "yes" ]; then
	       OLD_TIMEOUT="$(cat /sys/class/firmware/timeout)"
	fi

	OLD_FWPATH="$(cat /sys/module/firmware_class/parameters/path)"
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

test_finish()
{
	if [ "$HAS_FW_LOADER_USER_HELPER" = "yes" ]; then
		echo "$OLD_TIMEOUT" >/sys/class/firmware/timeout
	fi
	if [ "$OLD_FWPATH" = "" ]; then
		OLD_FWPATH=" "
	fi
	if [ "$TEST_REQS_FW_SET_CUSTOM_PATH" = "yes" ]; then
		echo -n "$OLD_FWPATH" >/sys/module/firmware_class/parameters/path
	fi
	if [ -f $FW ]; then
		rm -f "$FW"
	fi
	if [ -d $FWPATH ]; then
		rm -rf "$FWPATH"
	fi
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
