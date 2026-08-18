#!/bin/bash -e
# CoreSight context switch thread attribution (exclusive)

# SPDX-License-Identifier: GPL-2.0

# If CoreSight is not available, skip the test
perf list pmu | grep -q cs_etm || exit 2

if [ "$(id -u)" != 0 ]; then
	# Requires root for "-C 0" in record command
	echo "[Skip] No root permission"
	exit 2
fi

tmpdir=$(mktemp -d /tmp/__perf_test.coresight_context_switch.XXXXX)

cleanup() {
	rm -rf "${tmpdir}"
	trap - EXIT TERM INT
}

trap_cleanup() {
	cleanup
	exit 1
}
trap trap_cleanup EXIT TERM INT

check_samples() {
	owner_samples=$(grep -c "proc1.*context_switch_loop_proc1" "$tmpdir/script" || true)
	next_samples=$(grep -c "proc2.*context_switch_loop_proc2" "$tmpdir/script" || true)

	if [ "$owner_samples" -eq 0 ] || [ "$next_samples" -eq 0 ]; then
		echo "No samples found"
		cleanup
		exit 1
	fi

	if grep "proc2.*context_switch_loop_proc1" "$tmpdir/script"; then
		echo "Thread1 symbol was attributed to proc2"
		cleanup
		exit 1
	fi

	if grep "proc1.*context_switch_loop_proc2" "$tmpdir/script"; then
		echo "Thread2 symbol was attributed to proc1"
		cleanup
		exit 1
	fi
}

cf="$tmpdir/ctl"
af="$tmpdir/ack"
mkfifo "$cf" "$af"

# Pin to one CPU so the two threads alternate running but record into the same
# trace buffer. Start disabled and use the control FIFO to only record the
# workload and not startup.
perf record -o "$tmpdir/data" -e cs_etm/timestamp=0/u -C 0 -D -1 --control fifo:"$cf","$af" -- \
	taskset --cpu-list 0 perf test --record-ctl fifo:"$cf","$af" \
	-w context_switch_loop > /dev/null 2>&1

# Test both instruction and branch sample generation modes.
perf script -i "$tmpdir/data" --itrace=i4 -F comm,pid,tid,ip,sym > "$tmpdir/script" 2>/dev/null
check_samples
perf script -i "$tmpdir/data" --itrace=b -F comm,pid,tid,ip,sym > "$tmpdir/script" 2>/dev/null
check_samples

cleanup
exit 0
