#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

. "$(cd "$(dirname "$0")" && pwd)"/test_common.sh

TID="loop_04"
ERR_CODE=0

_prep_test "loop" "mkfs & mount & umount with zero copy"

_create_backfile 0 256M

dev_id=$(_add_ublk_dev -t loop -z "${UBLK_BACKFILES[0]}")
_check_add_dev $TID $?

_mkfs_mount_test /dev/ublkb"${dev_id}"
ERR_CODE=$?

_cleanup_test "loop"

_show_result $TID $ERR_CODE
