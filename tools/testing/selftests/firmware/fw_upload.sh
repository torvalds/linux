#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# This validates the user-initiated fw upload mechanism of the firmware
# loader. It verifies that one or more firmware devices can be created
# for a device driver. It also verifies the data transfer, the
# cancellation support, and the error flows.
set -e

TEST_REQS_FW_UPLOAD="yes"
TEST_DIR=$(dirname $0)

progress_states="preparing transferring  programming"
errors="hw-error
	timeout
	device-busy
	invalid-file-size
	read-write-error
	flash-wearout"
error_abort="user-abort"
fwname1=fw1
fwname2=fw2
fwname3=fw3

source $TEST_DIR/fw_lib.sh

check_mods
check_setup
verify_reqs

trap "upload_finish" EXIT

upload_finish() {
	local fwdevs="$fwname1 $fwname2 $fwname3"

	for name in $fwdevs; do
		if [ -e "$DIR/$name" ]; then
			echo -n "$name" > "$DIR"/upload_unregister
		fi
	done
}

upload_fw() {
	local name="$1"
	local file="$2"

	echo 1 > "$DIR"/"$name"/loading
	cat "$file" > "$DIR"/"$name"/data
	echo 0 > "$DIR"/"$name"/loading
}

verify_fw() {
	local name="$1"
	local file="$2"

	echo -n "$name" > "$DIR"/config_upload_name
	if ! cmp "$file" "$DIR"/upload_read > /dev/null 2>&1; then
		echo "$0: firmware compare for $name did not match" >&2
		exit 1
	fi

	echo "$0: firmware upload for $name works" >&2
	return 0
}

inject_error() {
	local name="$1"
	local status="$2"
	local error="$3"

	echo 1 > "$DIR"/"$name"/loading
	echo -n "inject":"$status":"$error" > "$DIR"/"$name"/data
	echo 0 > "$DIR"/"$name"/loading
}

await_status() {
	local name="$1"
	local expected="$2"
	local status
	local i

	let i=0
	while [ $i -lt 50 ]; do
		status=$(cat "$DIR"/"$name"/status)
		if [ "$status" = "$expected" ]; then
			return 0;
		fi
		sleep 1e-03
		let i=$i+1
	done

	echo "$0: Invalid status: Expected $expected, Actual $status" >&2
	return 1;
}

await_idle() {
	local name="$1"

	await_status "$name" "idle"
	return $?
}

expect_error() {
	local name="$1"
	local expected="$2"
	local error=$(cat "$DIR"/"$name"/error)

	if [ "$error" != "$expected" ]; then
		echo "Invalid error: Expected $expected, Actual $error" >&2
		return 1
	fi

	return 0
}

random_firmware() {
	local bs="$1"
	local count="$2"
	local file=$(mktemp -p /tmp uploadfwXXX.bin)

	dd if=/dev/urandom of="$file" bs="$bs" count="$count" > /dev/null 2>&1
	echo "$file"
}

test_upload_cancel() {
	local name="$1"
	local status

	for status in $progress_states; do
		inject_error $name $status $error_abort
		if ! await_status $name $status; then
			exit 1
		fi

		echo 1 > "$DIR"/"$name"/cancel

		if ! await_idle $name; then
			exit 1
		fi

		if ! expect_error $name "$status":"$error_abort"; then
			exit 1
		fi
	done

	echo "$0: firmware upload cancellation works"
	return 0
}

test_error_handling() {
	local name=$1
	local status
	local error

	for status in $progress_states; do
		for error in $errors; do
			inject_error $name $status $error

			if ! await_idle $name; then
				exit 1
			fi

			if ! expect_error $name "$status":"$error"; then
				exit 1
			fi

		done
	done
	echo "$0: firmware upload error handling works"
}

test_fw_too_big() {
	local name=$1
	local fw_too_big=`random_firmware 512 5`
	local expected="preparing:invalid-file-size"

	upload_fw $name $fw_too_big
	rm -f $fw_too_big

	if ! await_idle $name; then
		exit 1
	fi

	if ! expect_error $name $expected; then
		exit 1
	fi

	echo "$0: oversized firmware error handling works"
}

echo -n "$fwname1" > "$DIR"/upload_register
echo -n "$fwname2" > "$DIR"/upload_register
echo -n "$fwname3" > "$DIR"/upload_register

test_upload_cancel $fwname1
test_error_handling $fwname1
test_fw_too_big $fwname1

fw_file1=`random_firmware 512 4`
fw_file2=`random_firmware 512 3`
fw_file3=`random_firmware 512 2`

upload_fw $fwname1 $fw_file1
upload_fw $fwname2 $fw_file2
upload_fw $fwname3 $fw_file3

verify_fw ${fwname1} ${fw_file1}
verify_fw ${fwname2} ${fw_file2}
verify_fw ${fwname3} ${fw_file3}

echo -n "$fwname1" > "$DIR"/upload_unregister
echo -n "$fwname2" > "$DIR"/upload_unregister
echo -n "$fwname3" > "$DIR"/upload_unregister

exit 0
