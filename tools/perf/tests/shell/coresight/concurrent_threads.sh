#!/bin/bash -e
# CoreSight concurrent threads (exclusive)

# SPDX-License-Identifier: GPL-2.0

# If CoreSight is not available, skip the test
perf list pmu | grep -q cs_etm || exit 2

tmpdir=$(mktemp -d /tmp/__perf_test.coresight_concurrent_threads.XXXXX)

cleanup() {
	rm -rf "${tmpdir}"
	trap - EXIT TERM INT
}

trap_cleanup() {
	cleanup
	exit 1
}
trap trap_cleanup EXIT TERM INT

cf="$tmpdir/ctl"
af="$tmpdir/ack"
mkfifo "$cf" "$af"

nthreads=10

# Timestamps off to reduce trace size, start disabled and use the control FIFO
# to only record the workload and not startup.
perf record -o "$tmpdir/data" -e cs_etm/timestamp=0/u -D -1 --control fifo:"$cf","$af" \
	-- perf test --record-ctl fifo:"$cf","$af" -w named_threads $nthreads 1 > /dev/null 2>&1

perf script -i "$tmpdir/data" > "$tmpdir/script" 2>/dev/null

# Check all threads were traced and they have the correct thread name and symbol
for i in $(seq 1 $nthreads); do
	if ! grep -q "thread${i} .* named_threads_thread${i}" "$tmpdir/script"; then
		echo "Error: thread${i} missing" >&2
		cleanup
		exit 1
	fi
done

cleanup
exit 0
