#!/bin/bash
# perf script --dlfilter tests
# SPDX-License-Identifier: GPL-2.0

set -e

shelldir=$(dirname "$0")
# shellcheck source=lib/setup_python.sh
. "${shelldir}"/lib/setup_python.sh

# skip if there's no compiler
if ! [ -x "$(command -v cc)" ]; then
	echo "failed: no compiler, install gcc"
	exit 2
fi

err=0
perfdata=$(mktemp /tmp/__perf_test.perf.data.XXXXX)
dlfilter_c=$(mktemp /tmp/__perf_test.dlfilter.test.c.XXXXX)
dlfilter_so=$(mktemp /tmp/__perf_test.dlfilter.so.XXXXX)

cleanup() {
	rm -f "${perfdata}"
	rm -f "${dlfilter_c}"
	rm -f "${dlfilter_so}"
	rm -f "${dlfilter_so}.o"

	trap - EXIT TERM INT
}

trap_cleanup() {
	echo "Unexpected signal in ${FUNCNAME[1]}"
	cleanup
	exit 1
}
trap trap_cleanup EXIT TERM INT

cat <<EOF > "${dlfilter_c}"
#include <perf/perf_dlfilter.h>
#include <string.h>
#include <stdio.h>

struct perf_dlfilter_fns perf_dlfilter_fns;

int filter_event(void *data, const struct perf_dlfilter_sample *sample, void *ctx)
{
	const struct perf_dlfilter_al *al;

	if (!sample->ip)
		return 0;

	al = perf_dlfilter_fns.resolve_ip(ctx);
	if (!al || !al->sym || strcmp(al->sym, "test_loop"))
		return 1;

	return 0;
}
EOF

test_dlfilter() {
	echo "Basic --dlfilter test"
	# Generate perf.data file
	if ! perf record -o "${perfdata}" perf test -w thloop 1 2> /dev/null
	then
		echo "Basic --dlfilter test [Failed record]"
		err=1
		return
	fi

	# Build the dlfilter
	if ! cc -c -I tools/perf/include -fpic -x c "${dlfilter_c}" -o "${dlfilter_so}.o"
	then
		echo "Basic --dlfilter test [Failed to build dlfilter object]"
		err=1
		return
	fi

	if ! cc -shared -o "${dlfilter_so}" "${dlfilter_so}.o"
	then
		echo "Basic --dlfilter test [Failed to link dlfilter shared object]"
		err=1
		return
	fi

	# Check that the output contains "test_loop" and nothing else
	if ! perf script -i "${perfdata}" --dlfilter "${dlfilter_so}" | grep -q "test_loop"
	then
		echo "Basic --dlfilter test [Failed missing output]"
		err=1
		return
	fi

	# The filter should filter out everything except test_loop, so ensure no other symbols are present
	# This is a simple check; we could be more rigorous
	if perf script -i "${perfdata}" --dlfilter "${dlfilter_so}" | grep -v "test_loop" | grep -q "perf"
	then
		echo "Basic --dlfilter test [Failed filtering]"
		err=1
		return
	fi

	echo "Basic --dlfilter test [Success]"
}

test_dlfilter
cleanup
exit $err
