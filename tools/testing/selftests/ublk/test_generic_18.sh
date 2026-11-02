#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

. "$(cd "$(dirname "$0")" && pwd)"/test_common.sh

ERR_CODE=0
CANCEL_PROG="$(_ublk_test_top_dir)/ublk_cancel_ready"

_prep_test "generic" "start device over canceled io commands"

# STOP_DEV while kublk's UBLK_F_BATCH_IO server prepares and fetches: the
# server has to fail cleanly when PREP or FETCH is aborted, so `kublk add`
# must return either way.
race_batch_stop()
{
	local id=9999 i res started=0

	_have_feature "BATCH_IO" || return "$UBLK_SKIP_CODE"
	for i in $(seq 1 20); do
		timeout 20 "$UBLK_PROG" add -t null -q 2 -b -n "$id" > /dev/null 2>&1 &
		local pid=$!
		# vary where STOP_DEV lands relative to the batch PREP and FETCH
		sleep "0.$((i % 10))"
		"$UBLK_PROG" stop -n "$id" > /dev/null 2>&1
		wait "$pid"
		res=$?
		"$UBLK_PROG" del -n "$id" > /dev/null 2>&1
		if [ "$res" -eq 124 ]; then
			echo "run $i: kublk add -b hung after STOP_DEV"
			return 255
		fi
		[ "$res" -eq 0 ] && started=$((started + 1))
	done
	echo "20 runs: started $started, stopped before start $((20 - started))"
}

# race_batch_stop is above, the other modes are described in
# ublk_cancel_ready.c
for mode in stop_start partial_fetch recovery stop_restart stop_attached \
		stop_live_restart race_start race_fetch race_async_fetch \
		race_batch_stop; do
	dmesg_before=$(dmesg | wc -l)
	if [ "$mode" = race_batch_stop ]; then
		race_batch_stop > "$UBLK_TMP" 2>&1
	else
		timeout 60 "$CANCEL_PROG" "$mode" > "$UBLK_TMP" 2>&1
	fi
	res=$?
	msg=""

	if dmesg | tail -n +"$((dmesg_before + 1))" | \
			grep -q -e "BUG:" -e "Oops" -e "WARNING:"; then
		msg="$mode: kernel oops/warning"
		ERR_CODE=255
	elif [ "$res" -eq "$UBLK_SKIP_CODE" ]; then
		[ "$ERR_CODE" -eq 0 ] && ERR_CODE=$UBLK_SKIP_CODE
	elif [ "$res" -ne 0 ]; then
		msg="$mode: failed ($res)"
		ERR_CODE=255
	fi
	# the output once: on failure, or always when not quiet
	[ -n "$msg" ] && echo "$msg"
	if [ -n "$msg" ] || [ "$UBLK_TEST_QUIET" -eq 0 ]; then
		cat "$UBLK_TMP"
	fi
done

_cleanup_test
_show_result $TID $ERR_CODE
