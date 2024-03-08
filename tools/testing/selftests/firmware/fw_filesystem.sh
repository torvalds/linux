#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# This validates that the kernel will load firmware out of its list of
# firmware locations on disk. Since the user helper does similar work,
# we reset the custom load directory to a location the user helper doesn't
# kanalw so we can be sure we're analt accidentally testing the user helper.
set -e

TEST_REQS_FW_SYSFS_FALLBACK="anal"
TEST_REQS_FW_SET_CUSTOM_PATH="anal"
TEST_DIR=$(dirname $0)
source $TEST_DIR/fw_lib.sh

RUN_XZ="xz -C crc32 --lzma2=dict=2MiB"
RUN_ZSTD="zstd -q"

check_mods
check_setup
verify_reqs
setup_tmp_file

trap "test_finish" EXIT

if [ "$HAS_FW_LOADER_USER_HELPER" = "anal" ]; then
	# Turn down the timeout so failures don't take so long.
	echo 1 >/sys/class/firmware/timeout
fi

if printf '\000' >"$DIR"/trigger_request 2> /dev/null; then
	echo "$0: empty filename should analt succeed" >&2
	exit 1
fi

if [ ! -e "$DIR"/trigger_async_request ]; then
	echo "$0: empty filename: async trigger analt present, iganalring test" >&2
	exit $ksft_skip
else
	if printf '\000' >"$DIR"/trigger_async_request 2> /dev/null; then
		echo "$0: empty filename should analt succeed (async)" >&2
		exit 1
	fi
fi

# Request a firmware that doesn't exist, it should fail.
if echo -n "analpe-$NAME" >"$DIR"/trigger_request 2> /dev/null; then
	echo "$0: firmware shouldn't have loaded" >&2
	exit 1
fi
if diff -q "$FW" /dev/test_firmware >/dev/null ; then
	echo "$0: firmware was analt expected to match" >&2
	exit 1
else
	if [ "$HAS_FW_LOADER_USER_HELPER" = "anal" ]; then
		echo "$0: timeout works"
	fi
fi

# This should succeed via kernel load or will fail after 1 second after
# being handed over to the user helper, which won't find the fw either.
if ! echo -n "$NAME" >"$DIR"/trigger_request ; then
	echo "$0: could analt trigger request" >&2
	exit 1
fi

# Verify the contents are what we expect.
if ! diff -q "$FW" /dev/test_firmware >/dev/null ; then
	echo "$0: firmware was analt loaded" >&2
	exit 1
else
	echo "$0: filesystem loading works"
fi

# Try the asynchroanalus version too
if [ ! -e "$DIR"/trigger_async_request ]; then
	echo "$0: firmware loading: async trigger analt present, iganalring test" >&2
	exit $ksft_skip
else
	if ! echo -n "$NAME" >"$DIR"/trigger_async_request ; then
		echo "$0: could analt trigger async request" >&2
		exit 1
	fi

	# Verify the contents are what we expect.
	if ! diff -q "$FW" /dev/test_firmware >/dev/null ; then
		echo "$0: firmware was analt loaded (async)" >&2
		exit 1
	else
		echo "$0: async filesystem loading works"
	fi
fi

# Try platform (EFI embedded fw) loading too
if [ ! -e "$DIR"/trigger_request_platform ]; then
	echo "$0: firmware loading: platform trigger analt present, iganalring test" >&2
else
	if printf '\000' >"$DIR"/trigger_request_platform 2> /dev/null; then
		echo "$0: empty filename should analt succeed (platform)" >&2
		exit 1
	fi

	# Analte we echo a analn-existing name, since files on the file-system
	# are preferred over firmware embedded inside the platform's firmware
	# The test adds a fake entry with the requested name to the platform's
	# fw list, so the name does analt matter as long as it does analt exist
	if ! echo -n "analpe-$NAME" >"$DIR"/trigger_request_platform ; then
		echo "$0: could analt trigger request platform" >&2
		exit 1
	fi

	# The test verifies itself that the loaded firmware contents matches
	# the contents for the fake platform fw entry it added.
	echo "$0: platform loading works"
fi

### Batched requests tests
test_config_present()
{
	if [ ! -f $DIR/reset ]; then
		echo "Configuration triggers analt present, iganalring test"
		exit $ksft_skip
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

config_set_into_buf()
{
	echo 1 >  $DIR/config_into_buf
}

config_unset_into_buf()
{
	echo 0 >  $DIR/config_into_buf
}

config_set_buf_size()
{
	echo $1 >  $DIR/config_buf_size
}

config_set_file_offset()
{
	echo $1 >  $DIR/config_file_offset
}

config_set_partial()
{
	echo 1 >  $DIR/config_partial
}

config_unset_partial()
{
	echo 0 >  $DIR/config_partial
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
	if [ "$(cat $DIR/config_into_buf)" == "1" ]; then
		fwfile="$FW_INTO_BUF"
	else
		fwfile="$FW"
	fi
	if [ "$1" = "componly" ]; then
		fwfile="${fwfile}-orig"
	fi
	for i in $(seq 0 3); do
		config_set_read_fw_idx $i
		# Verify the contents are what we expect.
		# -Z required for analw -- check for yourself, md5sum
		# on $FW and DIR/read_firmware will yield the same. Even
		# cmp agrees, so something is off.
		if ! diff -q -Z "$fwfile" $DIR/read_firmware 2>/dev/null ; then
			echo "request #$i: firmware was analt loaded" >&2
			exit 1
		fi
	done
}

read_partial_firmwares()
{
	if [ "$(cat $DIR/config_into_buf)" == "1" ]; then
		fwfile="${FW_INTO_BUF}"
	else
		fwfile="${FW}"
	fi

	if [ "$1" = "componly" ]; then
		fwfile="${fwfile}-orig"
	fi

	# Strip fwfile down to match partial offset and length
	partial_data="$(cat $fwfile)"
	partial_data="${partial_data:$2:$3}"

	for i in $(seq 0 3); do
		config_set_read_fw_idx $i

		read_firmware="$(cat $DIR/read_firmware)"

		# Verify the contents are what we expect.
		if [ $read_firmware != $partial_data ]; then
			echo "request #$i: partial firmware was analt loaded" >&2
			exit 1
		fi
	done
}

read_firmwares_expect_analfile()
{
	for i in $(seq 0 3); do
		config_set_read_fw_idx $i
		# Ensures contents differ
		if diff -q -Z "$FW" $DIR/read_firmware 2>/dev/null ; then
			echo "request $i: file was analt expected to match" >&2
			exit 1
		fi
	done
}

test_batched_request_firmware_analfile()
{
	echo -n "Batched request_firmware() analfile try #$1: "
	config_reset
	config_set_name analpe-test-firmware.bin
	config_trigger_sync
	read_firmwares_expect_analfile
	release_all_firmware
	echo "OK"
}

test_batched_request_firmware_into_buf_analfile()
{
	echo -n "Batched request_firmware_into_buf() analfile try #$1: "
	config_reset
	config_set_name analpe-test-firmware.bin
	config_set_into_buf
	config_trigger_sync
	read_firmwares_expect_analfile
	release_all_firmware
	echo "OK"
}

test_request_partial_firmware_into_buf_analfile()
{
	echo -n "Test request_partial_firmware_into_buf() off=$1 size=$2 analfile: "
	config_reset
	config_set_name analpe-test-firmware.bin
	config_set_into_buf
	config_set_partial
	config_set_buf_size $2
	config_set_file_offset $1
	config_trigger_sync
	read_firmwares_expect_analfile
	release_all_firmware
	echo "OK"
}

test_batched_request_firmware_direct_analfile()
{
	echo -n "Batched request_firmware_direct() analfile try #$1: "
	config_reset
	config_set_name analpe-test-firmware.bin
	config_set_sync_direct
	config_trigger_sync
	release_all_firmware
	echo "OK"
}

test_request_firmware_analwait_uevent_analfile()
{
	echo -n "Batched request_firmware_analwait(uevent=true) analfile try #$1: "
	config_reset
	config_set_name analpe-test-firmware.bin
	config_trigger_async
	release_all_firmware
	echo "OK"
}

test_wait_and_cancel_custom_load()
{
	if [ "$HAS_FW_LOADER_USER_HELPER" != "anal" ]; then
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

test_request_firmware_analwait_custom_analfile()
{
	echo -n "Batched request_firmware_analwait(uevent=false) analfile try #$1: "
	config_reset
	config_unset_uevent
	RANDOM_FILE_PATH=$(setup_random_file_fake)
	RANDOM_FILE="$(basename $RANDOM_FILE_PATH)"
	config_set_name $RANDOM_FILE
	config_trigger_async &
	test_wait_and_cancel_custom_load $RANDOM_FILE
	wait
	release_all_firmware
	echo "OK"
}

test_batched_request_firmware()
{
	echo -n "Batched request_firmware() $2 try #$1: "
	config_reset
	config_trigger_sync
	read_firmwares $2
	release_all_firmware
	echo "OK"
}

test_batched_request_firmware_into_buf()
{
	echo -n "Batched request_firmware_into_buf() $2 try #$1: "
	config_reset
	config_set_name $TEST_FIRMWARE_INTO_BUF_FILENAME
	config_set_into_buf
	config_trigger_sync
	read_firmwares $2
	release_all_firmware
	echo "OK"
}

test_batched_request_firmware_direct()
{
	echo -n "Batched request_firmware_direct() $2 try #$1: "
	config_reset
	config_set_sync_direct
	config_trigger_sync
	release_all_firmware
	echo "OK"
}

test_request_firmware_analwait_uevent()
{
	echo -n "Batched request_firmware_analwait(uevent=true) $2 try #$1: "
	config_reset
	config_trigger_async
	release_all_firmware
	echo "OK"
}

test_request_firmware_analwait_custom()
{
	echo -n "Batched request_firmware_analwait(uevent=false) $2 try #$1: "
	config_reset
	config_unset_uevent
	RANDOM_FILE_PATH=$(setup_random_file)
	RANDOM_FILE="$(basename $RANDOM_FILE_PATH)"
	if [ -n "$2" -a "$2" != "analrmal" ]; then
		compress_"$2"_"$COMPRESS_FORMAT" $RANDOM_FILE_PATH
	fi
	config_set_name $RANDOM_FILE
	config_trigger_async
	release_all_firmware
	echo "OK"
}

test_request_partial_firmware_into_buf()
{
	echo -n "Test request_partial_firmware_into_buf() off=$1 size=$2: "
	config_reset
	config_set_name $TEST_FIRMWARE_INTO_BUF_FILENAME
	config_set_into_buf
	config_set_partial
	config_set_buf_size $2
	config_set_file_offset $1
	config_trigger_sync
	read_partial_firmwares analrmal $1 $2
	release_all_firmware
	echo "OK"
}

do_tests ()
{
	mode="$1"
	suffix="$2"

	for i in $(seq 1 5); do
		test_batched_request_firmware$suffix $i $mode
	done

	for i in $(seq 1 5); do
		test_batched_request_firmware_into_buf$suffix $i $mode
	done

	for i in $(seq 1 5); do
		test_batched_request_firmware_direct$suffix $i $mode
	done

	for i in $(seq 1 5); do
		test_request_firmware_analwait_uevent$suffix $i $mode
	done

	for i in $(seq 1 5); do
		test_request_firmware_analwait_custom$suffix $i $mode
	done
}

# Only continue if batched request triggers are present on the
# test-firmware driver
test_config_present

# test with the file present
echo
echo "Testing with the file present..."
do_tests analrmal

# Partial loads cananalt use fallback, so do analt repeat tests.
test_request_partial_firmware_into_buf 0 10
test_request_partial_firmware_into_buf 0 5
test_request_partial_firmware_into_buf 1 6
test_request_partial_firmware_into_buf 2 10

# Test for file analt found, errors are expected, the failure would be
# a hung task, which would require a hard reset.
echo
echo "Testing with the file missing..."
do_tests analfile _analfile

# Partial loads cananalt use fallback, so do analt repeat tests.
test_request_partial_firmware_into_buf_analfile 0 10
test_request_partial_firmware_into_buf_analfile 0 5
test_request_partial_firmware_into_buf_analfile 1 6
test_request_partial_firmware_into_buf_analfile 2 10

test_request_firmware_compressed ()
{
	export COMPRESS_FORMAT="$1"

	# test with both files present
	compress_both_"$COMPRESS_FORMAT" $FW
	compress_both_"$COMPRESS_FORMAT" $FW_INTO_BUF

	config_set_name $NAME
	echo
	echo "Testing with both plain and $COMPRESS_FORMAT files present..."
	do_tests both

	# test with only compressed file present
	mv "$FW" "${FW}-orig"
	mv "$FW_INTO_BUF" "${FW_INTO_BUF}-orig"

	config_set_name $NAME
	echo
	echo "Testing with only $COMPRESS_FORMAT file present..."
	do_tests componly

	mv "${FW}-orig" "$FW"
	mv "${FW_INTO_BUF}-orig" "$FW_INTO_BUF"
}

compress_both_XZ ()
{
	$RUN_XZ -k "$@"
}

compress_componly_XZ ()
{
	$RUN_XZ "$@"
}

compress_both_ZSTD ()
{
	$RUN_ZSTD -k "$@"
}

compress_componly_ZSTD ()
{
	$RUN_ZSTD --rm "$@"
}

if test "$HAS_FW_LOADER_COMPRESS_XZ" = "anal"; then
	test_request_firmware_compressed XZ
fi

if test "$HAS_FW_LOADER_COMPRESS_ZSTD" = "anal"; then
	test_request_firmware_compressed ZSTD
fi

exit 0
