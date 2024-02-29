#!/bin/sh
# perf pipe recording and injection test
# SPDX-License-Identifier: GPL-2.0

shelldir=$(dirname "$0")
# shellcheck source=lib/perf_has_symbol.sh
. "${shelldir}"/lib/perf_has_symbol.sh

sym="noploop"

skip_test_missing_symbol ${sym}

data=$(mktemp /tmp/perf.data.XXXXXX)
prog="perf test -w noploop"
task="perf"

if ! perf record -e task-clock:u -o - ${prog} | perf report -i - --task | grep ${task}; then
	echo "cannot find the test file in the perf report"
	exit 1
fi

if ! perf record -e task-clock:u -o - ${prog} | perf inject -b | perf report -i - | grep ${sym}; then
	echo "cannot find noploop function in pipe #1"
	exit 1
fi

perf record -e task-clock:u -o - ${prog} | perf inject -b -o ${data}
if ! perf report -i ${data} | grep ${sym}; then
	echo "cannot find noploop function in pipe #2"
	exit 1
fi

perf record -e task-clock:u -o ${data} ${prog}
if ! perf inject -b -i ${data} | perf report -i - | grep ${sym}; then
	echo "cannot find noploop function in pipe #3"
	exit 1
fi


rm -f ${data} ${data}.old
exit 0
