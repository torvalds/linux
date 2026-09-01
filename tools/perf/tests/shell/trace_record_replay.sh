#!/bin/bash
# perf trace record and replay
# SPDX-License-Identifier: GPL-2.0

# Check that perf trace works with record and replay

# shellcheck source=lib/probe.sh
. "$(dirname $0)"/lib/probe.sh
# shellcheck source=lib/perf_record.sh
. "$(dirname $0)"/lib/perf_record.sh

skip_if_no_perf_trace || exit 2
[ "$(id -u)" = 0 ] || exit 2

file=$(mktemp /tmp/temporary_file.XXXXX)
err=0

cleanup() {
	rm -f ${file}
	perf_record_cleanup
	trap - EXIT INT TERM
}

trap_cleanup() {
	echo "Unexpected signal in ${FUNCNAME[1]}"
	cleanup
	exit 1
}
trap trap_cleanup EXIT INT TERM

check_nanosleep() {
  perf trace -i "${file}" 2>&1 | grep -q nanosleep
}

PERF_RECORD_CMD="perf trace record" perf_record_with_retry "${file}" "check_nanosleep" "sleep"
err=$?

if [ $err -ne 0 ]; then
	if [ $err -eq 2 ]; then
		logfile="${PERF_RECORD_LOGS[${#PERF_RECORD_LOGS[@]}-1]}"
		echo "perf trace record failed. Log output:"
		cat "$logfile"
	else
		echo "Failed: cannot find *nanosleep syscall"
	fi
	cleanup
	exit 1
fi

cleanup
exit 0
