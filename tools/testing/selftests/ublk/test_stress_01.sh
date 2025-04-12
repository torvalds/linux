#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

. "$(cd "$(dirname "$0")" && pwd)"/test_common.sh
TID="stress_01"
ERR_CODE=0

ublk_io_and_remove()
{
	local size=$1
	local dev_id
	shift 1

	dev_id=$(_add_ublk_dev "$@")
	_check_add_dev $TID $?

	[ "$UBLK_TEST_QUIET" -eq 0 ] && echo "run ublk IO vs. remove device(ublk add $*)"
	if ! __run_io_and_remove "$dev_id" "${size}" "no"; then
		echo "/dev/ublkc$dev_id isn't removed"
		exit 255
	fi
}

_prep_test "stress" "run IO and remove device"

ublk_io_and_remove 8G -t null -q 4
ERR_CODE=$?
if [ ${ERR_CODE} -ne 0 ]; then
	_show_result $TID $ERR_CODE
fi

_create_backfile 0 256M

ublk_io_and_remove 256M -t loop -q 4 "${UBLK_BACKFILES[0]}"
ERR_CODE=$?
if [ ${ERR_CODE} -ne 0 ]; then
	_show_result $TID $ERR_CODE
fi

ublk_io_and_remove 256M -t loop -q 4 -z "${UBLK_BACKFILES[0]}"
ERR_CODE=$?
_cleanup_test "stress"
_show_result $TID $ERR_CODE
