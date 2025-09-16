#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

. "$(cd "$(dirname "$0")" && pwd)"/test_common.sh

TID="generic_10"
ERR_CODE=0

if ! _have_feature "UPDATE_SIZE"; then
	exit "$UBLK_SKIP_CODE"
fi

_prep_test "null" "check update size"

dev_id=$(_add_ublk_dev -t null)
_check_add_dev $TID $?

size=$(_get_disk_size /dev/ublkb"${dev_id}")
size=$(( size / 2 ))
if ! "$UBLK_PROG" update_size -n "$dev_id" -s "$size"; then
	ERR_CODE=255
fi

new_size=$(_get_disk_size /dev/ublkb"${dev_id}")
if [ "$new_size" != "$size" ]; then
	ERR_CODE=255
fi

_cleanup_test "null"
_show_result $TID $ERR_CODE
