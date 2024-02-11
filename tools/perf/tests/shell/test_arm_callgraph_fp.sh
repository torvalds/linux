#!/bin/sh
# Check Arm64 callgraphs are complete in fp mode
# SPDX-License-Identifier: GPL-2.0

shelldir=$(dirname "$0")
# shellcheck source=lib/perf_has_symbol.sh
. "${shelldir}"/lib/perf_has_symbol.sh

lscpu | grep -q "aarch64" || exit 2

skip_test_missing_symbol leafloop

PERF_DATA=$(mktemp /tmp/__perf_test.perf.data.XXXXX)
TEST_PROGRAM="perf test -w leafloop"

cleanup_files()
{
	rm -f "$PERF_DATA"
}

trap cleanup_files EXIT TERM INT

# Add a 1 second delay to skip samples that are not in the leaf() function
# shellcheck disable=SC2086
perf record -o "$PERF_DATA" --call-graph fp -e cycles//u -D 1000 --user-callchains -- $TEST_PROGRAM 2> /dev/null &
PID=$!

echo " + Recording (PID=$PID)..."
sleep 2
echo " + Stopping perf-record..."

kill $PID
wait $PID

# expected perf-script output:
#
# program
# 	728 leaf
# 	753 parent
# 	76c leafloop
# ...

perf script -i "$PERF_DATA" -F comm,ip,sym | head -n4
perf script -i "$PERF_DATA" -F comm,ip,sym | head -n4 | \
	awk '{ if ($2 != "") sym[i++] = $2 } END { if (sym[0] != "leaf" ||
						       sym[1] != "parent" ||
						       sym[2] != "leafloop") exit 1 }'
