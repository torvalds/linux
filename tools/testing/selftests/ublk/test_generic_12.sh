#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

. "$(cd "$(dirname "$0")" && pwd)"/test_common.sh

TID="generic_12"
ERR_CODE=0

if ! _have_program bpftrace; then
	exit "$UBLK_SKIP_CODE"
fi

if ! _have_program fio; then
	exit "$UBLK_SKIP_CODE"
fi

_prep_test "null" "do imbalanced load, it should be balanced over I/O threads"

NTHREADS=6
dev_id=$(_add_ublk_dev -t null -q 4 -d 16 --nthreads $NTHREADS --per_io_tasks)
_check_add_dev $TID $?

dev_t=$(_get_disk_dev_t "$dev_id")
bpftrace trace/count_ios_per_tid.bt "$dev_t" > "$UBLK_TMP" 2>&1 &
btrace_pid=$!
sleep 2

if ! kill -0 "$btrace_pid" > /dev/null 2>&1; then
	_cleanup_test "null"
	exit "$UBLK_SKIP_CODE"
fi

# do imbalanced I/O on the ublk device
# pin to cpu 0 to prevent migration/only target one queue
fio --name=write_seq \
    --filename=/dev/ublkb"${dev_id}" \
    --ioengine=libaio --iodepth=16 \
    --rw=write \
    --size=512M \
    --direct=1 \
    --bs=4k \
    --cpus_allowed=0 > /dev/null 2>&1
ERR_CODE=$?
kill "$btrace_pid"
wait

# check that every task handles some I/O, even though all I/O was issued
# from a single CPU. when ublk gets support for round-robin tag
# allocation, this check can be strengthened to assert that every thread
# handles the same number of I/Os
NR_THREADS_THAT_HANDLED_IO=$(grep -c '@' ${UBLK_TMP})
if [[ $NR_THREADS_THAT_HANDLED_IO -ne $NTHREADS ]]; then
        echo "only $NR_THREADS_THAT_HANDLED_IO handled I/O! expected $NTHREADS"
        cat "$UBLK_TMP"
        ERR_CODE=255
fi

_cleanup_test "null"
_show_result $TID $ERR_CODE
