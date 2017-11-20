#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# This validates that the kernel will load firmware out of its list of
# firmware locations on disk. Since the user helper does similar work,
# we reset the custom load directory to a location the user helper doesn't
# know so we can be sure we're not accidentally testing the user helper.
set -e

DIR=/sys/devices/virtual/misc/test_firmware
TEST_DIR=$(dirname $0)

test_modprobe()
{
	if [ ! -d $DIR ]; then
		echo "$0: $DIR not present"
		echo "You must have the following enabled in your kernel:"
		cat $TEST_DIR/config
		exit 1
	fi
}

trap "test_modprobe" EXIT

if [ ! -d $DIR ]; then
	modprobe test_firmware
fi

# CONFIG_FW_LOADER_USER_HELPER has a sysfs class under /sys/class/firmware/
# These days most distros enable CONFIG_FW_LOADER_USER_HELPER but disable
# CONFIG_FW_LOADER_USER_HELPER_FALLBACK. We use /sys/class/firmware/ as an
# indicator for CONFIG_FW_LOADER_USER_HELPER.
HAS_FW_LOADER_USER_HELPER=$(if [ -d /sys/class/firmware/ ]; then echo yes; else echo no; fi)

if [ "$HAS_FW_LOADER_USER_HELPER" = "yes" ]; then
	OLD_TIMEOUT=$(cat /sys/class/firmware/timeout)
fi

OLD_FWPATH=$(cat /sys/module/firmware_class/parameters/path)

FWPATH=$(mktemp -d)
FW="$FWPATH/test-firmware.bin"

test_finish()
{
	if [ "$HAS_FW_LOADER_USER_HELPER" = "yes" ]; then
		echo "$OLD_TIMEOUT" >/sys/class/firmware/timeout
	fi
	if [ "$OLD_FWPATH" = "" ]; then
		OLD_FWPATH=" "
	fi
	echo -n "$OLD_FWPATH" >/sys/module/firmware_class/parameters/path
	rm -f "$FW"
	rmdir "$FWPATH"
}

trap "test_finish" EXIT

if [ "$HAS_FW_LOADER_USER_HELPER" = "yes" ]; then
	# Turn down the timeout so failures don't take so long.
	echo 1 >/sys/class/firmware/timeout
fi

# Set the kernel search path.
echo -n "$FWPATH" >/sys/module/firmware_class/parameters/path

# This is an unlikely real-world firmware content. :)
echo "ABCD0123" >"$FW"

NAME=$(basename "$FW")

if printf '\000' >"$DIR"/trigger_request 2> /dev/null; then
	echo "$0: empty filename should not succeed" >&2
	exit 1
fi

if printf '\000' >"$DIR"/trigger_async_request 2> /dev/null; then
	echo "$0: empty filename should not succeed (async)" >&2
	exit 1
fi

# Request a firmware that doesn't exist, it should fail.
if echo -n "nope-$NAME" >"$DIR"/trigger_request 2> /dev/null; then
	echo "$0: firmware shouldn't have loaded" >&2
	exit 1
fi
if diff -q "$FW" /dev/test_firmware >/dev/null ; then
	echo "$0: firmware was not expected to match" >&2
	exit 1
else
	if [ "$HAS_FW_LOADER_USER_HELPER" = "yes" ]; then
		echo "$0: timeout works"
	fi
fi

# This should succeed via kernel load or will fail after 1 second after
# being handed over to the user helper, which won't find the fw either.
if ! echo -n "$NAME" >"$DIR"/trigger_request ; then
	echo "$0: could not trigger request" >&2
	exit 1
fi

# Verify the contents are what we expect.
if ! diff -q "$FW" /dev/test_firmware >/dev/null ; then
	echo "$0: firmware was not loaded" >&2
	exit 1
else
	echo "$0: filesystem loading works"
fi

# Try the asynchronous version too
if ! echo -n "$NAME" >"$DIR"/trigger_async_request ; then
	echo "$0: could not trigger async request" >&2
	exit 1
fi

# Verify the contents are what we expect.
if ! diff -q "$FW" /dev/test_firmware >/dev/null ; then
	echo "$0: firmware was not loaded (async)" >&2
	exit 1
else
	echo "$0: async filesystem loading works"
fi

### Batched requests tests
test_config_present()
{
	if [ ! -f $DIR/reset ]; then
		echo "Configuration triggers not present, ignoring test"
		exit 0
	fi
}

# Defaults :
#
# send_uevent: 1
# sync_direct: 0
# name: test-firmware.bin
# num_requests: 4
config_reset()
{
	echo 1 >  $DIR/reset
}

release_all_firmware()
{
	echo 1 >  $DIR/release_all_firmware
}

config_set_name()
{
	echo -n $1 >  $DIR/config_name
}

config_set_sync_direct()
{
	echo 1 >  $DIR/config_sync_direct
}

config_unset_sync_direct()
{
	echo 0 >  $DIR/config_sync_direct
}

config_set_uevent()
{
	echo 1 >  $DIR/config_send_uevent
}

config_unset_uevent()
{
	echo 0 >  $DIR/config_send_uevent
}

config_trigger_sync()
{
	echo -n 1 > $DIR/trigger_batched_requests 2>/dev/null
}

config_trigger_async()
{
	echo -n 1 > $DIR/trigger_batched_requests_async 2> /dev/null
}

config_set_read_fw_idx()
{
	echo -n $1 > $DIR/config_read_fw_idx 2> /dev/null
}

read_firmwares()
{
	for i in $(seq 0 3); do
		config_set_read_fw_idx $i
		# Verify the contents are what we expect.
		# -Z required for now -- check for yourself, md5sum
		# on $FW and DIR/read_firmware will yield the same. Even
		# cmp agrees, so something is off.
		if ! diff -q -Z "$FW" $DIR/read_firmware 2>/dev/null ; then
			echo "request #$i: firmware was not loaded" >&2
			exit 1
		fi
	done
}

read_firmwares_expect_nofile()
{
	for i in $(seq 0 3); do
		config_set_read_fw_idx $i
		# Ensures contents differ
		if diff -q -Z "$FW" $DIR/read_firmware 2>/dev/null ; then
			echo "request $i: file was not expected to match" >&2
			exit 1
		fi
	done
}

test_batched_request_firmware_nofile()
{
	echo -n "Batched request_firmware() nofile try #$1: "
	config_reset
	config_set_name nope-test-firmware.bin
	config_trigger_sync
	read_firmwares_expect_nofile
	release_all_firmware
	echo "OK"
}

test_batched_request_firmware_direct_nofile()
{
	echo -n "Batched request_firmware_direct() nofile try #$1: "
	config_reset
	config_set_name nope-test-firmware.bin
	config_set_sync_direct
	config_trigger_sync
	release_all_firmware
	echo "OK"
}

test_request_firmware_nowait_uevent_nofile()
{
	echo -n "Batched request_firmware_nowait(uevent=true) nofile try #$1: "
	config_reset
	config_set_name nope-test-firmware.bin
	config_trigger_async
	release_all_firmware
	echo "OK"
}

test_wait_and_cancel_custom_load()
{
	if [ "$HAS_FW_LOADER_USER_HELPER" != "yes" ]; then
		return
	fi
	local timeout=10
	name=$1
	while [ ! -e "$DIR"/"$name"/loading ]; do
		sleep 0.1
		timeout=$(( $timeout - 1 ))
		if [ "$timeout" -eq 0 ]; then
			echo "firmware interface never appeared:" >&2
			echo "$DIR/$name/loading" >&2
			exit 1
		fi
	done
	echo -1 >"$DIR"/"$name"/loading
}

test_request_firmware_nowait_custom_nofile()
{
	echo -n "Batched request_firmware_nowait(uevent=false) nofile try #$1: "
	config_unset_uevent
	config_set_name nope-test-firmware.bin
	config_trigger_async &
	test_wait_and_cancel_custom_load nope-test-firmware.bin
	wait
	release_all_firmware
	echo "OK"
}

test_batched_request_firmware()
{
	echo -n "Batched request_firmware() try #$1: "
	config_reset
	config_trigger_sync
	read_firmwares
	release_all_firmware
	echo "OK"
}

test_batched_request_firmware_direct()
{
	echo -n "Batched request_firmware_direct() try #$1: "
	config_reset
	config_set_sync_direct
	config_trigger_sync
	release_all_firmware
	echo "OK"
}

test_request_firmware_nowait_uevent()
{
	echo -n "Batched request_firmware_nowait(uevent=true) try #$1: "
	config_reset
	config_trigger_async
	release_all_firmware
	echo "OK"
}

test_request_firmware_nowait_custom()
{
	echo -n "Batched request_firmware_nowait(uevent=false) try #$1: "
	config_unset_uevent
	config_trigger_async
	release_all_firmware
	echo "OK"
}

# Only continue if batched request triggers are present on the
# test-firmware driver
test_config_present

# test with the file present
echo
echo "Testing with the file present..."
for i in $(seq 1 5); do
	test_batched_request_firmware $i
done

for i in $(seq 1 5); do
	test_batched_request_firmware_direct $i
done

for i in $(seq 1 5); do
	test_request_firmware_nowait_uevent $i
done

for i in $(seq 1 5); do
	test_request_firmware_nowait_custom $i
done

# Test for file not found, errors are expected, the failure would be
# a hung task, which would require a hard reset.
echo
echo "Testing with the file missing..."
for i in $(seq 1 5); do
	test_batched_request_firmware_nofile $i
done

for i in $(seq 1 5); do
	test_batched_request_firmware_direct_nofile $i
done

for i in $(seq 1 5); do
	test_request_firmware_nowait_uevent_nofile $i
done

for i in $(seq 1 5); do
	test_request_firmware_nowait_custom_nofile $i
done

exit 0
