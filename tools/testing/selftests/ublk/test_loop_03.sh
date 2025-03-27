#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

. "$(cd "$(dirname "$0")" && pwd)"/test_common.sh

TID="loop_03"
ERR_CODE=0

if ! _have_program fio; then
	exit "$UBLK_SKIP_CODE"
fi

_prep_test "loop" "write and verify over zero copy"

backfile_0=$(_create_backfile 256M)
dev_id=$(_add_ublk_dev -t loop -z "$backfile_0")
_check_add_dev $TID $? "$backfile_0"

# run fio over the ublk disk
_run_fio_verify_io --filename=/dev/ublkb"${dev_id}" --size=256M
ERR_CODE=$?

_cleanup_test "loop"

_remove_backfile "$backfile_0"

_show_result $TID $ERR_CODE
