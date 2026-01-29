#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

. "$(cd "$(dirname "$0")" && pwd)"/test_common.sh

ERR_CODE=0

format_backing_file()
{
	local backing_file=$1

	# Create ublk device to write partition table
	local tmp_dev=$(_add_ublk_dev -t loop "${backing_file}")
	[ $? -ne 0 ] && return 1

	# Write partition table with sfdisk
	sfdisk /dev/ublkb"${tmp_dev}" > /dev/null 2>&1 <<EOF
label: dos
start=2048, size=100MiB, type=83
start=206848, size=100MiB, type=83
EOF
	local ret=$?

	"${UBLK_PROG}" del -n "${tmp_dev}"

	return $ret
}

test_auto_part_scan()
{
	local backing_file=$1

	# Create device WITHOUT --no_auto_part_scan
	local dev_id=$(_add_ublk_dev -t loop "${backing_file}")
	[ $? -ne 0 ] && return 1

	udevadm settle

	# Partitions should be auto-detected
	if [ ! -e /dev/ublkb"${dev_id}"p1 ] || [ ! -e /dev/ublkb"${dev_id}"p2 ]; then
		"${UBLK_PROG}" del -n "${dev_id}"
		return 1
	fi

	"${UBLK_PROG}" del -n "${dev_id}"
	return 0
}

test_no_auto_part_scan()
{
	local backing_file=$1

	# Create device WITH --no_auto_part_scan
	local dev_id=$(_add_ublk_dev -t loop --no_auto_part_scan "${backing_file}")
	[ $? -ne 0 ] && return 1

	udevadm settle

	# Partitions should NOT be auto-detected
	if [ -e /dev/ublkb"${dev_id}"p1 ]; then
		"${UBLK_PROG}" del -n "${dev_id}"
		return 1
	fi

	# Manual scan should work
	blockdev --rereadpt /dev/ublkb"${dev_id}" > /dev/null 2>&1
	udevadm settle

	if [ ! -e /dev/ublkb"${dev_id}"p1 ] || [ ! -e /dev/ublkb"${dev_id}"p2 ]; then
		"${UBLK_PROG}" del -n "${dev_id}"
		return 1
	fi

	"${UBLK_PROG}" del -n "${dev_id}"
	return 0
}

if ! _have_program sfdisk || ! _have_program blockdev; then
	exit "$UBLK_SKIP_CODE"
fi

_prep_test "generic" "test UBLK_F_NO_AUTO_PART_SCAN"

if ! _have_feature "UBLK_F_NO_AUTO_PART_SCAN"; then
	_cleanup_test "generic"
	exit "$UBLK_SKIP_CODE"
fi


# Create and format backing file with partition table
_create_backfile 0 256M
format_backing_file "${UBLK_BACKFILES[0]}"
[ $? -ne 0 ] && ERR_CODE=255

# Test normal auto partition scan
[ "$ERR_CODE" -eq 0 ] && test_auto_part_scan "${UBLK_BACKFILES[0]}"
[ $? -ne 0 ] && ERR_CODE=255

# Test no auto partition scan with manual scan
[ "$ERR_CODE" -eq 0 ] && test_no_auto_part_scan "${UBLK_BACKFILES[0]}"
[ $? -ne 0 ] && ERR_CODE=255

_cleanup_test "generic"
_show_result $TID $ERR_CODE
