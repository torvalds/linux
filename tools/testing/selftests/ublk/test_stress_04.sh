#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

. "$(cd "$(dirname "$0")" && pwd)"/test_common.sh
TID="stress_04"
ERR_CODE=0

ublk_io_and_kill_daemon()
{
	run_io_and_kill_daemon "$@"
	ERR_CODE=$?
	if [ ${ERR_CODE} -ne 0 ]; then
		echo "$TID failure: $*"
		_show_result $TID $ERR_CODE
	fi
}

if ! _have_program fio; then
	exit "$UBLK_SKIP_CODE"
fi
if ! _have_feature "ZERO_COPY"; then
	exit "$UBLK_SKIP_CODE"
fi

_prep_test "stress" "run IO and kill ublk server(zero copy)"

_create_backfile 0 256M
_create_backfile 1 128M
_create_backfile 2 128M

ublk_io_and_kill_daemon 8G -t null -q 4 -z &
ublk_io_and_kill_daemon 256M -t loop -q 4 -z "${UBLK_BACKFILES[0]}" &
ublk_io_and_kill_daemon 256M -t stripe -q 4 -z "${UBLK_BACKFILES[1]}" "${UBLK_BACKFILES[2]}" &
wait

_cleanup_test "stress"
_show_result $TID $ERR_CODE
