#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

. "$(cd "$(dirname "$0")" && pwd)"/test_common.sh

ERR_CODE=0

if ! _have_feature "BATCH_IO"; then
	exit "$UBLK_SKIP_CODE"
fi

_prep_test "generic" "test basic function of UBLK_F_BATCH_IO"

_create_backfile 0 256M
_create_backfile 1 256M

dev_id=$(_add_ublk_dev -t loop -q 2 -b "${UBLK_BACKFILES[0]}")
_check_add_dev $TID $?

if ! _mkfs_mount_test /dev/ublkb"${dev_id}"; then
	_cleanup_test "generic"
	_show_result $TID 255
fi

dev_id=$(_add_ublk_dev -t stripe -b --auto_zc "${UBLK_BACKFILES[0]}" "${UBLK_BACKFILES[1]}")
_check_add_dev $TID $?
_mkfs_mount_test /dev/ublkb"${dev_id}"
ERR_CODE=$?

_cleanup_test "generic"
_show_result $TID $ERR_CODE
