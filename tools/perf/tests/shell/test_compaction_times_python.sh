#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# compaction-times python test

set -e

shelldir=$(dirname "$0")
# shellcheck source=lib/setup_python.sh
. "${shelldir}"/lib/setup_python.sh

# If we don't have the perf python module, we can't test
if ! "$PYTHON" -c 'import perf' > /dev/null 2>&1; then
	echo "Skipping test, perf python module not found"
	exit 2
fi

if ! perf check feature -q libtraceevent; then
	echo "Skipping test, perf built without libtraceevent"
	exit 2
fi

script_dir="$(dirname "$0")/../../python"
script_path="${script_dir}/compaction-times.py"

if [ ! -f "$script_path" ]; then
	echo "Skipping test, compaction-times.py not found at $script_path"
	exit 2
fi

err=0
temp_data=""
temp_out=""

cleanup() {
	rm -f "${temp_data}" "${temp_out}"
}

trap 'cleanup' EXIT TERM INT

temp_data=$(mktemp /tmp/perf.data.XXXXXX)
temp_out=$(mktemp /tmp/perf.out.XXXXXX)

test_file_mode() {
	echo "Testing compaction-times.py..."

	# Check for any compaction events to see if kernel supports it
	if ! perf list tracepoint | grep -q "compaction:mm_compaction_begin"; then
		echo "Skipping test, compaction tracepoints not found"
		exit 2
	fi

	# Generate some events
	# We might not naturally trigger compaction in 0.1s sleep, but the script
	# should parse the empty or sparse file correctly without crashing.
	if ! perf record -B -N --no-bpf-event \
		-e "compaction:mm_compaction_begin,compaction:mm_compaction_end" \
		-a -o "${temp_data}" -- sleep 0.1 >/dev/null 2>&1; then
		echo "Skipping test, perf record failed"
		exit 2
	fi

	# Run the script with some filters to validate filtering logic
	if ! perf script compaction-times -i "${temp_data}" > "${temp_out}"; then
		echo "File mode default test failed."
		err=1
	elif ! grep -q "total:" "${temp_out}"; then
		echo "File mode default test missing 'total:' output."
		err=1
	fi

	if ! perf script compaction-times -i "${temp_data}" "0-0" > "${temp_out}"; then
		echo "File mode strict PID filter test failed."
		err=1
	elif ! grep -q "total:" "${temp_out}"; then
		echo "File mode strict PID filter test missing 'total:' output."
		err=1
	fi

	if ! perf script compaction-times -i "${temp_data}" "sleep" > "${temp_out}"; then
		echo "File mode comm filter test failed."
		err=1
	elif ! grep -q "total:" "${temp_out}"; then
		echo "File mode comm filter test missing 'total:' output."
		err=1
	fi

	# Deterministic unit test of comm filtering and non-zero duration aggregation
	if ! "$PYTHON" -c "
import re, sys, importlib.util
spec = importlib.util.spec_from_file_location('compaction_times', sys.argv[1])
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

class DummyThread:
    def comm(self): return 'sleep'

class DummySession:
    def find_thread(self, pid, tid): return DummyThread()

class DummySample:
    def __init__(self, evsel, time, **kw):
        self.evsel = evsel
        self.sample_time = time
        self.sample_pid = 100
        self.sample_tid = 100
        for k, v in kw.items():
            setattr(self, k, v)

mod.Chead.add_filter(mod.get_comm_filter(re.compile('sleep')))
mod.session = DummySession()
mod.process_event(DummySample('evsel(compaction:mm_compaction_begin)', 1000))
mod.process_event(DummySample('evsel(compaction:mm_compaction_end)', 2500))
mod.trace_end()
" "$script_path" > "${temp_out}"; then
		echo "Deterministic comm filter unit test failed."
		err=1
	elif ! grep -q "total: 1500ns" "${temp_out}"; then
		echo "Deterministic comm filter unit test did not report expected 1500ns total."
		err=1
	fi

	if [ $err -eq 0 ]; then
		echo "File mode test passed."
	fi
}

test_file_mode

exit $err
