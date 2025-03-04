#!/bin/sh
# perf trace exit race
# SPDX-License-Identifier: GPL-2.0

# Check that the last events of a perf trace'd subprocess are not
# lost. Specifically, trace the exiting syscall of "true" 10 times and ensure
# the output contains 10 correct lines.

# shellcheck source=lib/probe.sh
. "$(dirname $0)"/lib/probe.sh

skip_if_no_perf_trace || exit 2
[ "$(id -u)" = 0 ] || exit 2

if [ "$1" = "-v" ]; then
	verbose="1"
fi

iter=10
regexp=" +[0-9]+\.[0-9]+ [0-9]+ syscalls:sys_enter_exit_group\(\)$"

trace_shutdown_race() {
	for _ in $(seq $iter); do
		perf trace --no-comm -e syscalls:sys_enter_exit_group true 2>>$file
	done
	result="$(grep -c -E "$regexp" $file)"
	[ $result = $iter ]
}


file=$(mktemp /tmp/temporary_file.XXXXX)

# Do not use whatever ~/.perfconfig file, it may change the output
# via trace.{show_timestamp,show_prefix,etc}
export PERF_CONFIG=/dev/null

trace_shutdown_race
err=$?

if [ $err != 0 ] && [ "${verbose}" = "1" ]; then
	lines_not_matching=$(mktemp /tmp/temporary_file.XXXXX)
	if grep -v -E "$regexp" $file > $lines_not_matching ; then
		echo "Lines not matching the expected regexp: '$regexp':"
		cat $lines_not_matching
	else
		echo "Missing output, expected $iter but only got $result"
	fi
	rm -f $lines_not_matching
fi

rm -f ${file}
exit $err
