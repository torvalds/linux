#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

. "$(cd "$(dirname "$0")" && pwd)"/test_common.sh
TID="stress_01"
ERR_CODE=0
DEV_ID=-1

ublk_io_and_remove()
{
	local size=$1
	shift 1
	local backfile=""
	if echo "$@" | grep -q "loop"; then
		backfile=${*: -1}
	fi
	DEV_ID=$(_add_ublk_dev "$@")
	_check_add_dev $TID $? "${backfile}"

	[ "$UBLK_TEST_QUIET" -eq 0 ] && echo "run ublk IO vs. remove device(ublk add $*)"
	if ! __run_io_and_remove "${DEV_ID}" "${size}" "no"; then
		echo "/dev/ublkc${DEV_ID} isn't removed"
		_remove_backfile "${backfile}"
		exit 255
	fi
}

_prep_test "stress" "run IO and remove device"

ublk_io_and_remove 8G -t null -q 4
ERR_CODE=$?
if [ ${ERR_CODE} -ne 0 ]; then
	_show_result $TID $ERR_CODE
fi

BACK_FILE=$(_create_backfile 256M)
ublk_io_and_remove 256M -t loop -q 4 "${BACK_FILE}"
ERR_CODE=$?
if [ ${ERR_CODE} -ne 0 ]; then
	_show_result $TID $ERR_CODE
fi

ublk_io_and_remove 256M -t loop -q 4 -z "${BACK_FILE}"
ERR_CODE=$?
_cleanup_test "stress"
_remove_backfile "${BACK_FILE}"
_show_result $TID $ERR_CODE
