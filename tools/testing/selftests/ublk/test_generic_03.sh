#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

. "$(cd "$(dirname "$0")" && pwd)"/test_common.sh

TID="generic_03"
ERR_CODE=0

_prep_test "null" "check dma & segment limits for zero copy"

dev_id=$(_add_ublk_dev -t null -z)
_check_add_dev $TID $?

sysfs_path=/sys/block/ublkb"${dev_id}"
dma_align=$(cat "$sysfs_path"/queue/dma_alignment)
max_segments=$(cat "$sysfs_path"/queue/max_segments)
max_segment_size=$(cat "$sysfs_path"/queue/max_segment_size)
if [ "$dma_align" != "4095" ]; then
	ERR_CODE=255
fi
if [ "$max_segments" != "32" ]; then
	ERR_CODE=255
fi
if [ "$max_segment_size" != "32768" ]; then
	ERR_CODE=255
fi
_cleanup_test "null"
_show_result $TID $ERR_CODE
