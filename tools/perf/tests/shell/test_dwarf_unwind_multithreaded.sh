#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Test libdw unwinding of multi-threaded processes (exclusive)

set -e

if ! perf check feature -q libdw-dwarf-unwind; then
	echo "Skip: libdw DWARF unwinding is not available"
	exit 2
fi

tmpdir=$(mktemp -d /tmp/perf-test-dwarf-unwind-multithreaded.XXXXXX)
perf_data="$tmpdir/perf.data"
perf_script="$tmpdir/perf-script.txt"
nr_threads=4
nr_worker_threads=$((nr_threads - 1))

cleanup()
{
	trap - EXIT TERM INT
	rm -rf "$tmpdir"
}

trap cleanup EXIT TERM INT

if ! perf record -q -e task-clock:u -F 99 --call-graph dwarf,8192 \
	-o "$perf_data" -- perf test -w thloop 2 "$nr_threads"
then
	echo "Skip: failed to record task-clock:u"
	exit 2
fi

if ! perf script --unwind-style=libdw \
	-F comm,pid,tid,event,ip,sym -i "$perf_data" > "$perf_script"
then
	echo "Error: failed to process the recording with libdw" >&2
	exit 1
fi

nr_unwound_threads=$(
	awk '
		BEGIN { RS = "" }

		# thfunc is the worker-only caller of test_loop. Finding it proves
		# that libdw unwound beyond the sampled leaf for this worker TID.
		/thfunc/ {
			split($2, id, "/")
			seen[id[2]] = 1
		}

		END {
			for (tid in seen)
				nr_tids++
			print nr_tids + 0
		}
	' "$perf_script"
)

if [ "$nr_unwound_threads" -ne "$nr_worker_threads" ]; then
	echo "Error: expected callchains for $nr_worker_threads worker TIDs," \
		"found $nr_unwound_threads" >&2
	exit 1
fi

exit 0
