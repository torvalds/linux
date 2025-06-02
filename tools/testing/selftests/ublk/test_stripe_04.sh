#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

. "$(cd "$(dirname "$0")" && pwd)"/test_common.sh

TID="stripe_04"
ERR_CODE=0

_prep_test "stripe" "mkfs & mount & umount on zero copy"

backfile_0=$(_create_backfile 256M)
backfile_1=$(_create_backfile 256M)
dev_id=$(_add_ublk_dev -t stripe -z -q 2 "$backfile_0" "$backfile_1")
_check_add_dev $TID $? "$backfile_0" "$backfile_1"

_mkfs_mount_test /dev/ublkb"${dev_id}"
ERR_CODE=$?

_cleanup_test "stripe"

_remove_backfile "$backfile_0"
_remove_backfile "$backfile_1"

_show_result $TID $ERR_CODE
