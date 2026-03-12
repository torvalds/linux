#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

. "$(cd "$(dirname "$0")" && pwd)"/test_common.sh

ERR_CODE=0

if ! _have_program fio; then
	exit "$UBLK_SKIP_CODE"
fi

_prep_test "stripe" "write and verify test on user copy"

_create_backfile 0 256M
_create_backfile 1 256M

dev_id=$(_add_ublk_dev -t stripe -q 2 -u "${UBLK_BACKFILES[0]}" "${UBLK_BACKFILES[1]}")
_check_add_dev $TID $?

# run fio over the ublk disk
_run_fio_verify_io --filename=/dev/ublkb"${dev_id}" --size=512M
ERR_CODE=$?

_cleanup_test "stripe"
_show_result $TID $ERR_CODE
