#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# export-to-postgresql python test (exclusive)

set -e

shelldir=$(dirname "$0")
# shellcheck source=lib/setup_python.sh
. "${shelldir}"/lib/setup_python.sh

if ! "$PYTHON" -c 'import perf' > /dev/null 2>&1; then
	echo "Skipping test, perf python module not found"
	exit 2
fi

# If we don't have psql, we can't test
if ! command -v psql >/dev/null 2>&1; then
	echo "Skipping test, psql not found"
	exit 2
fi

# Check if we can connect to postgres and create a database
if ! psql -c "SELECT 1" postgres >/dev/null 2>&1; then
	echo "Skipping test, cannot connect to postgres"
	exit 2
fi

script_dir="$(dirname "$0")/../../python"
script_path="${script_dir}/export-to-postgresql.py"

if [ ! -f "$script_path" ]; then
	echo "Skipping test, export-to-postgresql.py not found at $script_path"
	exit 2
fi

err=0
temp_dir=""
temp_data=""
temp_db=""

cleanup() {
	[ -n "${temp_dir}" ] && rm -rf "${temp_dir}"
	psql -c "DROP DATABASE IF EXISTS ${temp_db}" postgres >/dev/null 2>&1 || true
}

trap 'cleanup' EXIT
trap 'cleanup; exit 1' TERM INT

temp_dir=$(mktemp -d /tmp/perf.pg.XXXXXX)
temp_data="${temp_dir}/perf.data"
temp_db="perf_test_db_$$"

test_file_mode() {
	echo "Testing export-to-postgresql.py..."

	# Verify that invalid URI / key-value database names are rejected
	if perf script export-to-postgresql -i /dev/null \
		-o "dbname=foo host=evil" >/dev/null 2>&1; then
		echo "Connection parameter injection check failed."
		err=1
	fi

	# Generate events with callchains and context switches
	if ! perf record -B -N --no-bpf-event -g --switch-events -o "${temp_data}" \
	     -- perf test -w noploop >/dev/null 2>&1 && \
	   ! perf record -B -N --no-bpf-event -g -o "${temp_data}" \
	     -- perf test -w noploop >/dev/null 2>&1; then
		echo "Skipping test, perf record failed"
		exit 2
	fi

	# Ensure clean db start
	psql -c "DROP DATABASE IF EXISTS ${temp_db}" postgres >/dev/null 2>&1 || true

	# Run the script
	if ! perf script export-to-postgresql -i "${temp_data}" -o "${temp_db}" >/dev/null; then
		echo "File mode test failed."
		err=1
	else
		# Check DB
		if ! psql -d "${temp_db}" -t -c 'SELECT COUNT(*) FROM samples WHERE id > 0;' | \
			grep -q '[1-9]' || \
		   ! psql -d "${temp_db}" -t -c 'SELECT COUNT(*) FROM call_paths WHERE id > 0;' | \
			grep -q '[1-9]'; then
			echo "PostgreSQL validation failed."
			err=1
		else
			echo "File mode test passed."
		fi
	fi
}

test_intel_pt() {
	echo "Testing export-to-postgresql.py with intel_pt..."

	# Generate some intel_pt events; sleep briefly after uname in the subshell
	# so uname's AUX buffer is flushed before SIGCHLD stops perf record.
	passed=0
	for _ in 1 2 3 4 5; do
		psql -c "DROP DATABASE IF EXISTS ${temp_db}" postgres >/dev/null 2>&1 || true
		rm -f "${temp_data}"
		if ! perf record -B -N --no-bpf-event -e intel_pt//u -o "${temp_data}" \
			-- sh -c "uname; sleep 0.05" >/dev/null 2>&1; then
			echo "Skipping intel_pt test, intel_pt not available."
			return 0
		fi

		# Run the script with --itrace cr to synthesize call_returns
		if perf script export-to-postgresql -i "${temp_data}" \
			-o "${temp_db}" --itrace cr >/dev/null && \
		   psql -d "${temp_db}" -t -c 'SELECT COUNT(*) FROM calls WHERE id > 0;' | \
			grep -q '[1-9]'; then
			passed=1
			break
		fi
	done

	if [ "$passed" -eq 0 ]; then
		echo "PostgreSQL intel_pt validation failed (no calls found)."
		err=1
	else
		echo "intel_pt test passed (cr validated)."
	fi
}

test_file_mode
test_intel_pt

exit $err
