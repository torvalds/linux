#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

. "$(cd "$(dirname "$0")" && pwd)"/test_common.sh

TID="generic_07"
ERR_CODE=0

if ! _have_program fio; then
	exit "$UBLK_SKIP_CODE"
fi

_prep_test "generic" "test UBLK_F_NEED_GET_DATA"

_create_backfile 0 256M
dev_id=$(_add_ublk_dev -t loop -q 2 -g "${UBLK_BACKFILES[0]}")
_check_add_dev $TID $?

# run fio over the ublk disk
_run_fio_verify_io --filename=/dev/ublkb"${dev_id}" --size=256M
ERR_CODE=$?
if [ "$ERR_CODE" -eq 0 ]; then
	_mkfs_mount_test /dev/ublkb"${dev_id}"
	ERR_CODE=$?
fi

_cleanup_test "generic"
_show_result $TID $ERR_CODE
