#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

. "$(cd "$(dirname "$0")" && pwd)"/test_common.sh

TID="generic_05"
ERR_CODE=0

ublk_run_recover_test()
{
	run_io_and_recover "kill_daemon" "$@"
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

_prep_test "recover" "basic recover function verification (zero copy)"

_create_backfile 0 256M
_create_backfile 1 128M
_create_backfile 2 128M

ublk_run_recover_test -t null -q 2 -r 1 -z &
ublk_run_recover_test -t loop -q 2 -r 1 -z "${UBLK_BACKFILES[0]}" &
ublk_run_recover_test -t stripe -q 2 -r 1 -z "${UBLK_BACKFILES[1]}" "${UBLK_BACKFILES[2]}" &
wait

ublk_run_recover_test -t null -q 2 -r 1 -z -i 1 &
ublk_run_recover_test -t loop -q 2 -r 1 -z -i 1 "${UBLK_BACKFILES[0]}" &
ublk_run_recover_test -t stripe -q 2 -r 1 -z -i 1 "${UBLK_BACKFILES[1]}" "${UBLK_BACKFILES[2]}" &
wait

_cleanup_test "recover"
_show_result $TID $ERR_CODE
