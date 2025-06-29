#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

. "$(cd "$(dirname "$0")" && pwd)"/test_common.sh
TID="stress_02"
ERR_CODE=0

if ! _have_program fio; then
	exit "$UBLK_SKIP_CODE"
fi

ublk_io_and_kill_daemon()
{
	run_io_and_kill_daemon "$@"
	ERR_CODE=$?
	if [ ${ERR_CODE} -ne 0 ]; then
		echo "$TID failure: $*"
		_show_result $TID $ERR_CODE
	fi
}

_prep_test "stress" "run IO and kill ublk server"

_create_backfile 0 256M
_create_backfile 1 128M
_create_backfile 2 128M

for nr_queue in 1 4; do
	ublk_io_and_kill_daemon 8G -t null -q "$nr_queue" &
	ublk_io_and_kill_daemon 256M -t loop -q "$nr_queue" "${UBLK_BACKFILES[0]}" &
	ublk_io_and_kill_daemon 256M -t stripe -q "$nr_queue" "${UBLK_BACKFILES[1]}" "${UBLK_BACKFILES[2]}" &
	wait
done

_cleanup_test "stress"
_show_result $TID $ERR_CODE
