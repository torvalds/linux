#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

. "$(cd "$(dirname "$0")" && pwd)"/test_common.sh

ERR_CODE=0

if ! _have_program bpftrace; then
	exit "$UBLK_SKIP_CODE"
fi

if ! _have_program fio; then
	exit "$UBLK_SKIP_CODE"
fi

_prep_test "null" "ublk dispatch won't reorder IO for MQ"

dev_id=$(_add_ublk_dev -t null -q 2)
_check_add_dev $TID $?

dev_t=$(_get_disk_dev_t "$dev_id")
bpftrace trace/seq_io.bt "$dev_t" "W" 1 > "$UBLK_TMP" 2>&1 &
btrace_pid=$!

# Wait for bpftrace probes to be attached (BEGIN block prints BPFTRACE_READY)
for _ in $(seq 100); do
	grep -q "BPFTRACE_READY" "$UBLK_TMP" 2>/dev/null && break
	sleep 0.1
done

if ! kill -0 "$btrace_pid" 2>/dev/null; then
	_cleanup_test "null"
	exit "$UBLK_SKIP_CODE"
fi

# run fio over this ublk disk (pinned to CPU 0)
taskset -c 0 fio --name=write_seq \
    --filename=/dev/ublkb"${dev_id}" \
    --ioengine=libaio --iodepth=16 \
    --rw=write \
    --size=512M \
    --direct=1 \
    --bs=4k > /dev/null 2>&1
ERR_CODE=$?
kill "$btrace_pid"
wait

# Check for out-of-order completions detected by bpftrace
if grep -q "^out_of_order:" "$UBLK_TMP"; then
	echo "I/O reordering detected:"
	grep "^out_of_order:" "$UBLK_TMP"
	ERR_CODE=255
fi
_cleanup_test "null"
_show_result $TID $ERR_CODE
