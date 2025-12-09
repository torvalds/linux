#!/bin/bash
# perf kwork tests
# SPDX-License-Identifier: GPL-2.0

set -e

# Root permissions required for tracing events.
if [ "$(id -u)" != 0 ]; then
	echo "[Skip] No root permission"
	exit 2
fi

err=0
perfdata=$(mktemp /tmp/__perf_test_kwork.perf.data.XXXXX)

cleanup() {
	rm -f "${perfdata}"
	rm -f "${perfdata}".old

	trap - EXIT TERM INT
}

trap_cleanup() {
	echo "Unexpected signal in ${FUNCNAME[1]}"
	cleanup
	exit 1
}
trap trap_cleanup EXIT TERM INT

test_kwork_record() {
	echo "Kwork record"
	perf kwork record -o "${perfdata}" -- sleep 1
	echo "Kwork record [Success]"
}

test_kwork_report() {
	echo "Kwork report"
	if ! perf kwork report -i "${perfdata}" | grep -q "Kwork Name"; then
		echo "Kwork report [Failed missing output]"
		err=1
	fi
	echo "Kwork report [Success]"
}

test_kwork_latency() {
	echo "Kwork latency"
	if ! perf kwork latency -i "${perfdata}" | grep -q "Avg delay"; then
		echo "Kwork latency [Failed missing output]"
		err=1
	fi
	echo "Kwork latency [Success]"
}

test_kwork_timehist() {
	echo "Kwork timehist"
	if ! perf kwork timehist -i "${perfdata}" | grep -q "Kwork name"; then
		echo "Kwork timehist [Failed missing output]"
		err=1
	fi
	echo "Kwork timehist [Success]"
}

test_kwork_top() {
	echo "Kwork top"
	if ! perf kwork top -i "${perfdata}" | grep -q "COMMAND"; then
		echo "Kwork top [Failed missing output]"
		err=1
	fi
	echo "Kwork top [Success]"
}

test_kwork_record
test_kwork_report
test_kwork_latency
test_kwork_timehist
test_kwork_top

cleanup
exit $err
