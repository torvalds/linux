#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

. "$(cd "$(dirname "$0")" && pwd)"/test_common.sh
TID="stress_05"
ERR_CODE=0

if ! _have_program fio; then
	exit "$UBLK_SKIP_CODE"
fi

run_io_and_remove()
{
	local size=$1
	local dev_id
	local dev_pid
	shift 1

	dev_id=$(_add_ublk_dev "$@")
	_check_add_dev $TID $?

	[ "$UBLK_TEST_QUIET" -eq 0 ] && echo "run ublk IO vs. remove device(ublk add $*)"

	fio --name=job1 --filename=/dev/ublkb"${dev_id}" --ioengine=libaio \
		--rw=readwrite --iodepth=128 --size="${size}" --numjobs=4 \
		--runtime=40 --time_based > /dev/null 2>&1 &
	sleep 4

	dev_pid=$(_get_ublk_daemon_pid "$dev_id")
	kill -9 "$dev_pid"

	if ! __remove_ublk_dev_return "${dev_id}"; then
		echo "delete dev ${dev_id} failed"
		return 255
	fi
}

ublk_io_and_remove()
{
	run_io_and_remove "$@"
	ERR_CODE=$?
	if [ ${ERR_CODE} -ne 0 ]; then
		echo "$TID failure: $*"
		_show_result $TID $ERR_CODE
	fi
}

_prep_test "stress" "run IO and remove device with recovery enabled"

_create_backfile 0 256M
_create_backfile 1 256M

for reissue in $(seq 0 1); do
	ublk_io_and_remove 8G -t null -q 4 -g -r 1 -i "$reissue" &
	ublk_io_and_remove 256M -t loop -q 4 -g -r 1 -i "$reissue" "${UBLK_BACKFILES[0]}" &
	wait
done

if _have_feature "ZERO_COPY"; then
	for reissue in $(seq 0 1); do
		ublk_io_and_remove 8G -t null -q 4 -g -z -r 1 -i "$reissue" &
		ublk_io_and_remove 256M -t loop -q 4 -g -z -r 1 -i "$reissue" "${UBLK_BACKFILES[1]}" &
		wait
	done
fi

if _have_feature "AUTO_BUF_REG"; then
	for reissue in $(seq 0 1); do
		ublk_io_and_remove 8G -t null -q 4 -g --auto_zc -r 1 -i "$reissue" &
		ublk_io_and_remove 256M -t loop -q 4 -g --auto_zc -r 1 -i "$reissue" "${UBLK_BACKFILES[1]}" &
		ublk_io_and_remove 8G -t null -q 4 -g -z --auto_zc --auto_zc_fallback -r 1 -i "$reissue" &
		wait
	done
fi

if _have_feature "PER_IO_DAEMON"; then
	ublk_io_and_remove 8G -t null -q 4 --nthreads 8 --per_io_tasks -r 1 -i "$reissue" &
	ublk_io_and_remove 256M -t loop -q 4 --nthreads 8 --per_io_tasks -r 1 -i "$reissue" "${UBLK_BACKFILES[0]}" &
	ublk_io_and_remove 8G -t null -q 4 --nthreads 8 --per_io_tasks -r 1 -i "$reissue"  &
fi
wait

_cleanup_test "stress"
_show_result $TID $ERR_CODE
