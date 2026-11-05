#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# export-to-sqlite python test (exclusive)

set -e

shelldir=$(dirname "$0")
# shellcheck source=lib/setup_python.sh
. "${shelldir}"/lib/setup_python.sh

if ! "$PYTHON" -c 'import perf' > /dev/null 2>&1; then
	echo "Skipping test, perf python module not found"
	exit 2
fi

# If we don't have sqlite3, we can't test
if ! "$PYTHON" -c 'import sqlite3' > /dev/null 2>&1; then
	echo "Skipping test, sqlite3 module not found"
	exit 2
fi

script_dir="$(dirname "$0")/../../python"
script_path="${script_dir}/export-to-sqlite.py"

if [ ! -f "$script_path" ]; then
	echo "Skipping test, export-to-sqlite.py not found at $script_path"
	exit 2
fi

err=0
temp_dir=""
temp_data=""
temp_db=""

cleanup() {
	[ -n "${temp_dir}" ] && rm -rf "${temp_dir}"
}

trap 'cleanup' EXIT
trap 'cleanup; exit 1' TERM INT

temp_dir=$(mktemp -d /tmp/perf.sqlite.XXXXXX)
temp_data="${temp_dir}/perf.data"
temp_db="${temp_dir}/perf.export.db"

test_file_mode() {
	echo "Testing export-to-sqlite.py..."

	# Generate events with callchains and context switches if supported
	if ! perf record -B -N --no-bpf-event -g --switch-events -o "${temp_data}" \
	     -- perf test -w noploop >/dev/null 2>&1 && \
	   ! perf record -B -N --no-bpf-event -g -o "${temp_data}" \
	     -- perf test -w noploop >/dev/null 2>&1; then
		echo "Skipping test, perf record failed"
		exit 2
	fi

	# Run the script
	if ! perf script export-to-sqlite -i "${temp_data}" -o "${temp_db}" >/dev/null; then
		echo "File mode test failed."
		err=1
	else
		# Check DB tables (samples, call_paths, threads, comms)
		query="import sqlite3; c = sqlite3.connect('${temp_db}'); "
		query="${query}s = c.execute('SELECT COUNT(*) FROM samples').fetchone()[0]; "
		query="${query}q = 'SELECT COUNT(*) FROM call_paths WHERE id > 0'; "
		query="${query}cp = c.execute(q).fetchone()[0]; "
		query="${query}exit(0 if s > 0 and cp > 0 else 1)"
		if ! "$PYTHON" -c "$query" >/dev/null 2>&1; then
			echo "SQLite validation failed."
			err=1
		else
			echo "File mode test passed."
		fi
	fi
}

test_intel_pt() {
	echo "Testing export-to-sqlite.py with intel_pt..."

	query="import sqlite3; c = sqlite3.connect('${temp_db}'); "
	query="${query}r = c.execute('SELECT COUNT(*) FROM calls').fetchone()[0]; "
	query="${query}exit(1 if r == 0 else 0)"

	# Generate some intel_pt events; sleep briefly after uname in the subshell
	# so uname's AUX buffer is flushed before SIGCHLD stops perf record.
	passed=0
	for _ in 1 2 3 4 5; do
		rm -f "${temp_db}" "${temp_data}"
		if ! perf record -B -N --no-bpf-event -e intel_pt//u -o "${temp_data}" \
			-- sh -c "uname; sleep 0.05" >/dev/null 2>&1; then
			echo "Skipping intel_pt test, intel_pt not available."
			return 0
		fi

		# Run the script with --itrace cr to synthesize call_returns
		if perf script export-to-sqlite -i "${temp_data}" -o "${temp_db}" --itrace cr && \
		   "$PYTHON" -c "$query" >/dev/null 2>&1; then
			passed=1
			break
		fi
	done

	if [ "$passed" -eq 0 ]; then
		echo "SQLite intel_pt validation failed (no calls found)."
		err=1
	else
		echo "intel_pt test passed (cr validated)."
	fi
}

test_file_mode
test_intel_pt

exit $err
