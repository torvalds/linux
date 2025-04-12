#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

. "$(cd "$(dirname "$0")" && pwd)"/test_common.sh
TID="stress_02"
ERR_CODE=0
DEV_ID=-1

ublk_io_and_kill_daemon()
{
	local size=$1
	shift 1

	DEV_ID=$(_add_ublk_dev "$@")
	_check_add_dev $TID $?

	[ "$UBLK_TEST_QUIET" -eq 0 ] && echo "run ublk IO vs kill ublk server(ublk add $*)"
	if ! __run_io_and_remove "${DEV_ID}" "${size}" "yes"; then
		echo "/dev/ublkc${DEV_ID} isn't removed res ${res}"
		exit 255
	fi
}

_prep_test "stress" "run IO and kill ublk server"

ublk_io_and_kill_daemon 8G -t null -q 4
ERR_CODE=$?
if [ ${ERR_CODE} -ne 0 ]; then
	_show_result $TID $ERR_CODE
fi

_create_backfile 0 256M

ublk_io_and_kill_daemon 256M -t loop -q 4 "${UBLK_BACKFILES[0]}"
ERR_CODE=$?
if [ ${ERR_CODE} -ne 0 ]; then
	_show_result $TID $ERR_CODE
fi

ublk_io_and_kill_daemon 256M -t loop -q 4 -z "${UBLK_BACKFILES[0]}"
ERR_CODE=$?
_cleanup_test "stress"
_show_result $TID $ERR_CODE
