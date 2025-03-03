#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

. "$(cd "$(dirname "$0")" && pwd)"/test_common.sh

TID="loop_04"
ERR_CODE=0

_prep_test "loop" "mkfs & mount & umount with zero copy"

backfile_0=$(_create_backfile 256M)
dev_id=$(_add_ublk_dev -t loop -z "$backfile_0")
_check_add_dev $TID $? "$backfile_0"

_mkfs_mount_test /dev/ublkb"${dev_id}"
ERR_CODE=$?

_cleanup_test "loop"

_remove_backfile "$backfile_0"

_show_result $TID $ERR_CODE
