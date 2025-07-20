#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

. "$(cd "$(dirname "$0")" && pwd)"/test_common.sh

TID="generic_11"
ERR_CODE=0

ublk_run_quiesce_recover()
{
	run_io_and_recover "quiesce_dev" "$@"
	ERR_CODE=$?
	if [ ${ERR_CODE} -ne 0 ]; then
		echo "$TID failure: $*"
		_show_result $TID $ERR_CODE
	fi
}

if ! _have_feature "QUIESCE"; then
	exit "$UBLK_SKIP_CODE"
fi

if ! _have_program fio; then
	exit "$UBLK_SKIP_CODE"
fi

_prep_test "quiesce" "basic quiesce & recover function verification"

_create_backfile 0 256M
_create_backfile 1 128M
_create_backfile 2 128M

ublk_run_quiesce_recover -t null -q 2 -r 1 &
ublk_run_quiesce_recover -t loop -q 2 -r 1 "${UBLK_BACKFILES[0]}" &
ublk_run_quiesce_recover -t stripe -q 2 -r 1 "${UBLK_BACKFILES[1]}" "${UBLK_BACKFILES[2]}" &
wait

ublk_run_quiesce_recover -t null -q 2 -r 1 -i 1 &
ublk_run_quiesce_recover -t loop -q 2 -r 1 -i 1 "${UBLK_BACKFILES[0]}" &
ublk_run_quiesce_recover -t stripe -q 2 -r 1 -i 1 "${UBLK_BACKFILES[1]}" "${UBLK_BACKFILES[2]}" &
wait

_cleanup_test "quiesce"
_show_result $TID $ERR_CODE
