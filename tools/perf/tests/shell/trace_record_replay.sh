#!/bin/sh
# perf trace record and replay
# SPDX-License-Identifier: GPL-2.0

# Check that perf trace works with record and replay

# shellcheck source=lib/probe.sh
. "$(dirname $0)"/lib/probe.sh

skip_if_no_perf_trace || exit 2
[ "$(id -u)" = 0 ] || exit 2

file=$(mktemp /tmp/temporary_file.XXXXX)

perf trace record -o ${file} sleep 1 || exit 1
if ! perf trace -i ${file} 2>&1 | grep nanosleep; then
	echo "Failed: cannot find *nanosleep syscall"
	exit 1
fi

rm -f ${file}
