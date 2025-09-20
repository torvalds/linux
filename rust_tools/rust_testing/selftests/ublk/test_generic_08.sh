#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

. "$(cd "$(dirname "$0")" && pwd)"/test_common.sh

TID="generic_08"
ERR_CODE=0

if ! _have_feature "AUTO_BUF_REG"; then
	exit "$UBLK_SKIP_CODE"
fi

_prep_test "generic" "test UBLK_F_AUTO_BUF_REG"

_create_backfile 0 256M
_create_backfile 1 256M

dev_id=$(_add_ublk_dev -t loop -q 2 --auto_zc "${UBLK_BACKFILES[0]}")
_check_add_dev $TID $?

if ! _mkfs_mount_test /dev/ublkb"${dev_id}"; then
	_cleanup_test "generic"
	_show_result $TID 255
fi

dev_id=$(_add_ublk_dev -t stripe --auto_zc "${UBLK_BACKFILES[0]}" "${UBLK_BACKFILES[1]}")
_check_add_dev $TID $?
_mkfs_mount_test /dev/ublkb"${dev_id}"
ERR_CODE=$?

_cleanup_test "generic"
_show_result $TID $ERR_CODE
