#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

. "$(cd "$(dirname "$0")" && pwd)"/test_common.sh

ERR_CODE=0

_check_value() {
	local name=$1
	local actual=$2
	local expected=$3

	if [ "$actual" != "$expected" ]; then
		echo "$name $actual != $expected"
		ERR_CODE=255
		return 1
	fi
	return 0
}

_test_metadata_only() {
	local dev_id

	dev_id=$(_add_ublk_dev -t null -u --no_auto_part_scan --metadata_size 8)
	_check_add_dev "$TID" $?

	_check_value "metadata_size" "$(_get_metadata_size "$dev_id" metadata_size)" 8 &&
	_check_value "pi_offset" "$(_get_metadata_size "$dev_id" pi_offset)" 0 &&
	_check_value "pi_tuple_size" "$(_get_metadata_size "$dev_id" pi_tuple_size)" 0 &&
	_check_value "device_is_integrity_capable" \
		"$(cat "/sys/block/ublkb$dev_id/integrity/device_is_integrity_capable")" 0 &&
	_check_value "format" "$(cat "/sys/block/ublkb$dev_id/integrity/format")" nop &&
	_check_value "protection_interval_bytes" \
		"$(cat "/sys/block/ublkb$dev_id/integrity/protection_interval_bytes")" 512 &&
	_check_value "tag_size" "$(cat "/sys/block/ublkb$dev_id/integrity/tag_size")" 0

	_ublk_del_dev "${dev_id}"
}

_test_integrity_capable_ip() {
	local dev_id

	dev_id=$(_add_ublk_dev -t null -u --no_auto_part_scan --integrity_capable --metadata_size 64 --pi_offset 56 --csum_type ip)
	_check_add_dev "$TID" $?

	_check_value "metadata_size" "$(_get_metadata_size "$dev_id" metadata_size)" 64 &&
	_check_value "pi_offset" "$(_get_metadata_size "$dev_id" pi_offset)" 56 &&
	_check_value "pi_tuple_size" "$(_get_metadata_size "$dev_id" pi_tuple_size)" 8 &&
	_check_value "device_is_integrity_capable" \
		"$(cat "/sys/block/ublkb$dev_id/integrity/device_is_integrity_capable")" 1 &&
	_check_value "format" "$(cat "/sys/block/ublkb$dev_id/integrity/format")" T10-DIF-TYPE3-IP &&
	_check_value "protection_interval_bytes" \
		"$(cat "/sys/block/ublkb$dev_id/integrity/protection_interval_bytes")" 512 &&
	_check_value "tag_size" "$(cat "/sys/block/ublkb$dev_id/integrity/tag_size")" 0

	_ublk_del_dev "${dev_id}"
}

_test_integrity_reftag_t10dif() {
	local dev_id

	dev_id=$(_add_ublk_dev -t null -u --no_auto_part_scan --integrity_reftag --metadata_size 8 --csum_type t10dif)
	_check_add_dev "$TID" $?

	_check_value "metadata_size" "$(_get_metadata_size "$dev_id" metadata_size)" 8 &&
	_check_value "pi_offset" "$(_get_metadata_size "$dev_id" pi_offset)" 0 &&
	_check_value "pi_tuple_size" "$(_get_metadata_size "$dev_id" pi_tuple_size)" 8 &&
	_check_value "device_is_integrity_capable" \
		"$(cat "/sys/block/ublkb$dev_id/integrity/device_is_integrity_capable")" 0 &&
	_check_value "format" "$(cat "/sys/block/ublkb$dev_id/integrity/format")" T10-DIF-TYPE1-CRC &&
	_check_value "protection_interval_bytes" \
		"$(cat "/sys/block/ublkb$dev_id/integrity/protection_interval_bytes")" 512 &&
	_check_value "tag_size" "$(cat "/sys/block/ublkb$dev_id/integrity/tag_size")" 0

	_ublk_del_dev "${dev_id}"
}

_test_nvme_csum() {
	local dev_id

	dev_id=$(_add_ublk_dev -t null -u --no_auto_part_scan --metadata_size 16 --csum_type nvme --tag_size 8)
	_check_add_dev "$TID" $?

	_check_value "metadata_size" "$(_get_metadata_size "$dev_id" metadata_size)" 16 &&
	_check_value "pi_offset" "$(_get_metadata_size "$dev_id" pi_offset)" 0 &&
	_check_value "pi_tuple_size" "$(_get_metadata_size "$dev_id" pi_tuple_size)" 16 &&
	_check_value "device_is_integrity_capable" \
		"$(cat "/sys/block/ublkb$dev_id/integrity/device_is_integrity_capable")" 0 &&
	_check_value "format" "$(cat "/sys/block/ublkb$dev_id/integrity/format")" EXT-DIF-TYPE3-CRC64 &&
	_check_value "protection_interval_bytes" \
		"$(cat "/sys/block/ublkb$dev_id/integrity/protection_interval_bytes")" 512 &&
	_check_value "tag_size" "$(cat "/sys/block/ublkb$dev_id/integrity/tag_size")" 8

	_ublk_del_dev "${dev_id}"
}

_prep_test "null" "integrity params"

_test_metadata_only
_test_integrity_capable_ip
_test_integrity_reftag_t10dif
_test_nvme_csum

_cleanup_test
_show_result "$TID" $ERR_CODE
