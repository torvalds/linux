#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

. "$(cd "$(dirname "$0")" && pwd)"/test_common.sh

TID="stripe_03"
ERR_CODE=0

if ! _have_program fio; then
	exit "$UBLK_SKIP_CODE"
fi

_prep_test "stripe" "write and verify test"

_create_backfile 0 256M
_create_backfile 1 256M

dev_id=$(_add_ublk_dev -q 2 -t stripe "${UBLK_BACKFILES[0]}" "${UBLK_BACKFILES[1]}")
_check_add_dev $TID $?

# run fio over the ublk disk
_run_fio_verify_io --filename=/dev/ublkb"${dev_id}" --size=512M
ERR_CODE=$?

_cleanup_test "stripe"
_show_result $TID $ERR_CODE
