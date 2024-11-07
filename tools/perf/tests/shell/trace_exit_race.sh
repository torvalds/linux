#!/bin/sh
# perf trace exit race
# SPDX-License-Identifier: GPL-2.0

# Check that the last events of a perf trace'd subprocess are not
# lost. Specifically, trace the exiting syscall of "true" 10 times and ensure
# the output contains 10 correct lines.

# shellcheck source=lib/probe.sh
. "$(dirname $0)"/lib/probe.sh

skip_if_no_perf_trace || exit 2

trace_shutdown_race() {
	for _ in $(seq 10); do
		perf trace -e syscalls:sys_enter_exit_group true 2>>$file
	done
	[ "$(grep -c -E ' +[0-9]+\.[0-9]+ +true/[0-9]+ syscalls:sys_enter_exit_group\(\)$' $file)" = "10" ]
}


file=$(mktemp /tmp/temporary_file.XXXXX)

# Do not use whatever ~/.perfconfig file, it may change the output
# via trace.{show_timestamp,show_prefix,etc}
export PERF_CONFIG=/dev/null

trace_shutdown_race
err=$?
rm -f ${file}
exit $err
