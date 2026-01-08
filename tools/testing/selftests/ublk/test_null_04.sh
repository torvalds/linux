#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

. "$(cd "$(dirname "$0")" && pwd)"/test_common.sh

TID=null_04

_prep_test "null" "integrity params"

dev_id=$(_add_ublk_dev -t null -u --metadata_size 8)
_check_add_dev $TID $?
metadata_size=$(_get_metadata_size "$dev_id" metadata_size)
if [ "$metadata_size" != 8 ]; then
	echo "metadata_size $metadata_size != 8"
	_show_result $TID 255
fi
pi_offset=$(_get_metadata_size "$dev_id" pi_offset)
if [ "$pi_offset" != 0 ]; then
	echo "pi_offset $pi_offset != 0"
	_show_result $TID 255
fi
pi_tuple_size=$(_get_metadata_size "$dev_id" pi_tuple_size)
if [ "$pi_tuple_size" != 0 ]; then
	echo "pi_tuple_size $pi_tuple_size != 0"
	_show_result $TID 255
fi
capable=$(cat "/sys/block/ublkb$dev_id/integrity/device_is_integrity_capable")
if [ "$capable" != 0 ]; then
	echo "device_is_integrity_capable $capable != 0"
	_show_result $TID 255
fi
format=$(cat "/sys/block/ublkb$dev_id/integrity/format")
if [ "$format" != nop ]; then
	echo "format $format != nop"
	_show_result $TID 255
fi
protection_interval_bytes=$(cat "/sys/block/ublkb$dev_id/integrity/protection_interval_bytes")
if [ "$protection_interval_bytes" != 512 ]; then
	echo "protection_interval_bytes $protection_interval_bytes != 512"
	_show_result $TID 255
fi
tag_size=$(cat "/sys/block/ublkb$dev_id/integrity/tag_size")
if [ "$tag_size" != 0 ]; then
	echo "tag_size $tag_size != 0"
	_show_result $TID 255
fi
_cleanup_test

dev_id=$(_add_ublk_dev -t null -u --integrity_capable --metadata_size 64 --pi_offset 56 --csum_type ip)
_check_add_dev $TID $?
metadata_size=$(_get_metadata_size "$dev_id" metadata_size)
if [ "$metadata_size" != 64 ]; then
	echo "metadata_size $metadata_size != 64"
	_show_result $TID 255
fi
pi_offset=$(_get_metadata_size "$dev_id" pi_offset)
if [ "$pi_offset" != 56 ]; then
	echo "pi_offset $pi_offset != 56"
	_show_result $TID 255
fi
pi_tuple_size=$(_get_metadata_size "$dev_id" pi_tuple_size)
if [ "$pi_tuple_size" != 8 ]; then
	echo "pi_tuple_size $pi_tuple_size != 8"
	_show_result $TID 255
fi
capable=$(cat "/sys/block/ublkb$dev_id/integrity/device_is_integrity_capable")
if [ "$capable" != 1 ]; then
	echo "device_is_integrity_capable $capable != 1"
	_show_result $TID 255
fi
format=$(cat "/sys/block/ublkb$dev_id/integrity/format")
if [ "$format" != T10-DIF-TYPE3-IP ]; then
	echo "format $format != T10-DIF-TYPE3-IP"
	_show_result $TID 255
fi
protection_interval_bytes=$(cat "/sys/block/ublkb$dev_id/integrity/protection_interval_bytes")
if [ "$protection_interval_bytes" != 512 ]; then
	echo "protection_interval_bytes $protection_interval_bytes != 512"
	_show_result $TID 255
fi
tag_size=$(cat "/sys/block/ublkb$dev_id/integrity/tag_size")
if [ "$tag_size" != 0 ]; then
	echo "tag_size $tag_size != 0"
	_show_result $TID 255
fi
_cleanup_test

dev_id=$(_add_ublk_dev -t null -u --integrity_reftag --metadata_size 8 --csum_type t10dif)
_check_add_dev $TID $?
metadata_size=$(_get_metadata_size "$dev_id" metadata_size)
if [ "$metadata_size" != 8 ]; then
	echo "metadata_size $metadata_size != 8"
	_show_result $TID 255
fi
pi_offset=$(_get_metadata_size "$dev_id" pi_offset)
if [ "$pi_offset" != 0 ]; then
	echo "pi_offset $pi_offset != 0"
	_show_result $TID 255
fi
pi_tuple_size=$(_get_metadata_size "$dev_id" pi_tuple_size)
if [ "$pi_tuple_size" != 8 ]; then
	echo "pi_tuple_size $pi_tuple_size != 8"
	_show_result $TID 255
fi
capable=$(cat "/sys/block/ublkb$dev_id/integrity/device_is_integrity_capable")
if [ "$capable" != 0 ]; then
	echo "device_is_integrity_capable $capable != 0"
	_show_result $TID 255
fi
format=$(cat "/sys/block/ublkb$dev_id/integrity/format")
if [ "$format" != T10-DIF-TYPE1-CRC ]; then
	echo "format $format != T10-DIF-TYPE1-CRC"
	_show_result $TID 255
fi
protection_interval_bytes=$(cat "/sys/block/ublkb$dev_id/integrity/protection_interval_bytes")
if [ "$protection_interval_bytes" != 512 ]; then
	echo "protection_interval_bytes $protection_interval_bytes != 512"
	_show_result $TID 255
fi
tag_size=$(cat "/sys/block/ublkb$dev_id/integrity/tag_size")
if [ "$tag_size" != 0 ]; then
	echo "tag_size $tag_size != 0"
	_show_result $TID 255
fi
_cleanup_test

dev_id=$(_add_ublk_dev -t null -u --metadata_size 16 --csum_type nvme --tag_size 8)
_check_add_dev $TID $?
metadata_size=$(_get_metadata_size "$dev_id" metadata_size)
if [ "$metadata_size" != 16 ]; then
	echo "metadata_size $metadata_size != 16"
	_show_result $TID 255
fi
pi_offset=$(_get_metadata_size "$dev_id" pi_offset)
if [ "$pi_offset" != 0 ]; then
	echo "pi_offset $pi_offset != 0"
	_show_result $TID 255
fi
pi_tuple_size=$(_get_metadata_size "$dev_id" pi_tuple_size)
if [ "$pi_tuple_size" != 16 ]; then
	echo "pi_tuple_size $pi_tuple_size != 16"
	_show_result $TID 255
fi
capable=$(cat "/sys/block/ublkb$dev_id/integrity/device_is_integrity_capable")
if [ "$capable" != 0 ]; then
	echo "device_is_integrity_capable $capable != 0"
	_show_result $TID 255
fi
format=$(cat "/sys/block/ublkb$dev_id/integrity/format")
if [ "$format" != EXT-DIF-TYPE3-CRC64 ]; then
	echo "format $format != EXT-DIF-TYPE3-CRC64"
	_show_result $TID 255
fi
protection_interval_bytes=$(cat "/sys/block/ublkb$dev_id/integrity/protection_interval_bytes")
if [ "$protection_interval_bytes" != 512 ]; then
	echo "protection_interval_bytes $protection_interval_bytes != 512"
	_show_result $TID 255
fi
tag_size=$(cat "/sys/block/ublkb$dev_id/integrity/tag_size")
if [ "$tag_size" != 8 ]; then
	echo "tag_size $tag_size != 8"
	_show_result $TID 255
fi
_cleanup_test

_show_result $TID 0
