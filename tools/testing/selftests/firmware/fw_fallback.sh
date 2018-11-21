#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# This validates that the kernel will fall back to using the fallback mechanism
# to load firmware it can't find on disk itself. We must request a firmware
# that the kernel won't find, and any installed helper (e.g. udev) also
# won't find so that we can do the load ourself manually.
set -e

TEST_REQS_FW_SYSFS_FALLBACK="yes"
TEST_REQS_FW_SET_CUSTOM_PATH="no"
TEST_DIR=$(dirname $0)
source $TEST_DIR/fw_lib.sh

check_mods
check_setup
verify_reqs
setup_tmp_file

trap "test_finish" EXIT

load_fw()
{
	local name="$1"
	local file="$2"

	# This will block until our load (below) has finished.
	echo -n "$name" >"$DIR"/trigger_request &

	# Give kernel a chance to react.
	local timeout=10
	while [ ! -e "$DIR"/"$name"/loading ]; do
		sleep 0.1
		timeout=$(( $timeout - 1 ))
		if [ "$timeout" -eq 0 ]; then
			echo "$0: firmware interface never appeared" >&2
			exit 1
		fi
	done

	echo 1 >"$DIR"/"$name"/loading
	cat "$file" >"$DIR"/"$name"/data
	echo 0 >"$DIR"/"$name"/loading

	# Wait for request to finish.
	wait
}

load_fw_cancel()
{
	local name="$1"
	local file="$2"

	# This will block until our load (below) has finished.
	echo -n "$name" >"$DIR"/trigger_request 2>/dev/null &

	# Give kernel a chance to react.
	local timeout=10
	while [ ! -e "$DIR"/"$name"/loading ]; do
		sleep 0.1
		timeout=$(( $timeout - 1 ))
		if [ "$timeout" -eq 0 ]; then
			echo "$0: firmware interface never appeared" >&2
			exit 1
		fi
	done

	echo -1 >"$DIR"/"$name"/loading

	# Wait for request to finish.
	wait
}

load_fw_custom()
{
	if [ ! -e "$DIR"/trigger_custom_fallback ]; then
		echo "$0: custom fallback trigger not present, ignoring test" >&2
		return 1
	fi

	local name="$1"
	local file="$2"

	echo -n "$name" >"$DIR"/trigger_custom_fallback 2>/dev/null &

	# Give kernel a chance to react.
	local timeout=10
	while [ ! -e "$DIR"/"$name"/loading ]; do
		sleep 0.1
		timeout=$(( $timeout - 1 ))
		if [ "$timeout" -eq 0 ]; then
			echo "$0: firmware interface never appeared" >&2
			exit 1
		fi
	done

	echo 1 >"$DIR"/"$name"/loading
	cat "$file" >"$DIR"/"$name"/data
	echo 0 >"$DIR"/"$name"/loading

	# Wait for request to finish.
	wait
	return 0
}


load_fw_custom_cancel()
{
	if [ ! -e "$DIR"/trigger_custom_fallback ]; then
		echo "$0: canceling custom fallback trigger not present, ignoring test" >&2
		return 1
	fi

	local name="$1"
	local file="$2"

	echo -n "$name" >"$DIR"/trigger_custom_fallback 2>/dev/null &

	# Give kernel a chance to react.
	local timeout=10
	while [ ! -e "$DIR"/"$name"/loading ]; do
		sleep 0.1
		timeout=$(( $timeout - 1 ))
		if [ "$timeout" -eq 0 ]; then
			echo "$0: firmware interface never appeared" >&2
			exit 1
		fi
	done

	echo -1 >"$DIR"/"$name"/loading

	# Wait for request to finish.
	wait
	return 0
}

load_fw_fallback_with_child()
{
	local name="$1"
	local file="$2"

	# This is the value already set but we want to be explicit
	echo 4 >/sys/class/firmware/timeout

	sleep 1 &
	SECONDS_BEFORE=$(date +%s)
	echo -n "$name" >"$DIR"/trigger_request 2>/dev/null
	SECONDS_AFTER=$(date +%s)
	SECONDS_DELTA=$(($SECONDS_AFTER - $SECONDS_BEFORE))
	if [ "$SECONDS_DELTA" -lt 4 ]; then
		RET=1
	else
		RET=0
	fi
	wait
	return $RET
}

test_syfs_timeout()
{
	DEVPATH="$DIR"/"nope-$NAME"/loading

	# Test failure when doing nothing (timeout works).
	echo -n 2 >/sys/class/firmware/timeout
	echo -n "nope-$NAME" >"$DIR"/trigger_request 2>/dev/null &

	# Give the kernel some time to load the loading file, must be less
	# than the timeout above.
	sleep 1
	if [ ! -f $DEVPATH ]; then
		echo "$0: fallback mechanism immediately cancelled"
		echo ""
		echo "The file never appeared: $DEVPATH"
		echo ""
		echo "This might be a distribution udev rule setup by your distribution"
		echo "to immediately cancel all fallback requests, this must be"
		echo "removed before running these tests. To confirm look for"
		echo "a firmware rule like /lib/udev/rules.d/50-firmware.rules"
		echo "and see if you have something like this:"
		echo ""
		echo "SUBSYSTEM==\"firmware\", ACTION==\"add\", ATTR{loading}=\"-1\""
		echo ""
		echo "If you do remove this file or comment out this line before"
		echo "proceeding with these tests."
		exit 1
	fi

	if diff -q "$FW" /dev/test_firmware >/dev/null ; then
		echo "$0: firmware was not expected to match" >&2
		exit 1
	else
		echo "$0: timeout works"
	fi
}

run_sysfs_main_tests()
{
	test_syfs_timeout
	# Put timeout high enough for us to do work but not so long that failures
	# slow down this test too much.
	echo 4 >/sys/class/firmware/timeout

	# Load this script instead of the desired firmware.
	load_fw "$NAME" "$0"
	if diff -q "$FW" /dev/test_firmware >/dev/null ; then
		echo "$0: firmware was not expected to match" >&2
		exit 1
	else
		echo "$0: firmware comparison works"
	fi

	# Do a proper load, which should work correctly.
	load_fw "$NAME" "$FW"
	if ! diff -q "$FW" /dev/test_firmware >/dev/null ; then
		echo "$0: firmware was not loaded" >&2
		exit 1
	else
		echo "$0: fallback mechanism works"
	fi

	load_fw_cancel "nope-$NAME" "$FW"
	if diff -q "$FW" /dev/test_firmware >/dev/null ; then
		echo "$0: firmware was expected to be cancelled" >&2
		exit 1
	else
		echo "$0: cancelling fallback mechanism works"
	fi

	set +e
	load_fw_fallback_with_child "nope-signal-$NAME" "$FW"
	if [ "$?" -eq 0 ]; then
		echo "$0: SIGCHLD on sync ignored as expected" >&2
	else
		echo "$0: error - sync firmware request cancelled due to SIGCHLD" >&2
		exit 1
	fi
	set -e
}

run_sysfs_custom_load_tests()
{
	RANDOM_FILE_PATH=$(setup_random_file)
	RANDOM_FILE="$(basename $RANDOM_FILE_PATH)"
	if load_fw_custom "$RANDOM_FILE" "$RANDOM_FILE_PATH" ; then
		if ! diff -q "$RANDOM_FILE_PATH" /dev/test_firmware >/dev/null ; then
			echo "$0: firmware was not loaded" >&2
			exit 1
		else
			echo "$0: custom fallback loading mechanism works"
		fi
	fi

	RANDOM_FILE_PATH=$(setup_random_file)
	RANDOM_FILE="$(basename $RANDOM_FILE_PATH)"
	if load_fw_custom "$RANDOM_FILE" "$RANDOM_FILE_PATH" ; then
		if ! diff -q "$RANDOM_FILE_PATH" /dev/test_firmware >/dev/null ; then
			echo "$0: firmware was not loaded" >&2
			exit 1
		else
			echo "$0: custom fallback loading mechanism works"
		fi
	fi

	RANDOM_FILE_REAL="$RANDOM_FILE_PATH"
	FAKE_RANDOM_FILE_PATH=$(setup_random_file_fake)
	FAKE_RANDOM_FILE="$(basename $FAKE_RANDOM_FILE_PATH)"

	if load_fw_custom_cancel "$FAKE_RANDOM_FILE" "$RANDOM_FILE_REAL" ; then
		if diff -q "$RANDOM_FILE_PATH" /dev/test_firmware >/dev/null ; then
			echo "$0: firmware was expected to be cancelled" >&2
			exit 1
		else
			echo "$0: cancelling custom fallback mechanism works"
		fi
	fi
}

if [ "$HAS_FW_LOADER_USER_HELPER_FALLBACK" = "yes" ]; then
	run_sysfs_main_tests
fi

run_sysfs_custom_load_tests

exit 0
