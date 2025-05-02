#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

. "$(cd "$(dirname "$0")" && pwd)"/test_common.sh

TID="generic_02"
ERR_CODE=0

if ! _have_program bpftrace; then
	exit "$UBLK_SKIP_CODE"
fi

_prep_test "null" "sequential io order for MQ"

dev_id=$(_add_ublk_dev -t null -q 2)
_check_add_dev $TID $?

dev_t=$(_get_disk_dev_t "$dev_id")
bpftrace trace/seq_io.bt "$dev_t" "W" 1 > "$UBLK_TMP" 2>&1 &
btrace_pid=$!
sleep 2

if ! kill -0 "$btrace_pid" > /dev/null 2>&1; then
	_cleanup_test "null"
	exit "$UBLK_SKIP_CODE"
fi

# run fio over this ublk disk
fio --name=write_seq \
    --filename=/dev/ublkb"${dev_id}" \
    --ioengine=libaio --iodepth=16 \
    --rw=write \
    --size=512M \
    --direct=1 \
    --bs=4k > /dev/null 2>&1
ERR_CODE=$?
kill "$btrace_pid"
wait
if grep -q "io_out_of_order" "$UBLK_TMP"; then
	cat "$UBLK_TMP"
	ERR_CODE=255
fi
_cleanup_test "null"
_show_result $TID $ERR_CODE
