#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# futex-contention python test

set -e

shelldir=$(dirname "$0")
# shellcheck source=lib/setup_python.sh
. "${shelldir}"/lib/setup_python.sh

# If we don't have the perf python module, we can't test
if ! "$PYTHON" -c 'import perf' > /dev/null 2>&1; then
	echo "Skipping test, perf python module not found"
	exit 2
fi

script_dir="$(dirname "$0")/../../python"
script_path="${script_dir}/futex-contention.py"

if ! perf check feature -q libtraceevent > /dev/null 2>&1; then
	echo "Skipping test, libtraceevent is disabled"
	exit 2
fi

if [ ! -f "$script_path" ]; then
	echo "Skipping test, futex-contention.py not found at $script_path"
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

test_synthetic() {
	echo "Testing futex-contention.py synthetic events..."
	if ! "$PYTHON" - "$script_path" << 'EOF'
import importlib.util
import sys
from types import SimpleNamespace

spec = importlib.util.spec_from_file_location("futex_contention", sys.argv[1])
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

enter_sample = SimpleNamespace(
    evsel="evsel(syscalls:sys_enter_futex:k)",
    sample_pid=1234,
    sample_tid=1234,
    uaddr=0xdeadbeef,
    op=mod.FUTEX_WAIT | mod.FUTEX_PRIVATE_FLAG,
    sample_time=1000,
)
exit_sample = SimpleNamespace(
    evsel="evsel(syscalls:sys_exit_futex:k)",
    sample_pid=1234,
    sample_tid=1234,
    sample_time=2500,
)

mod.process_event(enter_sample)
mod.process_event(exit_sample)

stats = mod.durations.get((1234, 0xdeadbeef))
assert stats is not None and stats.count == 1, f"Unexpected stats: {stats}"
assert stats.min_time == 1500 and stats.max_time == 1500 and stats.avg() == 1500.0
line = (f"{mod.process_names.get(1234, 'unknown')}[1234] lock {0xdeadbeef:x} "
        f"contended {stats.count} times, {stats.avg():.0f} avg ns "
        f"[max: {stats.max_time} ns, min {stats.min_time} ns]")
expected = "unknown[1234] lock deadbeef contended 1 times, 1500 avg ns [max: 1500 ns, min 1500 ns]"
assert line == expected, line
EOF
	then
		echo "Synthetic test failed."
		err=1
	else
		echo "Synthetic test passed."
	fi
}

test_file_mode() {
	echo "Testing futex-contention.py..."
	# Some systems might not have syscalls:sys_enter_futex
	if ! perf list tracepoint | grep -q syscalls:sys_enter_futex; then
		echo "Skipping file mode test, syscalls:sys_enter_futex not found"
		return
	fi

	# Generate some futex events
	if ! perf record -B -N --no-bpf-event \
		-e syscalls:sys_enter_futex,syscalls:sys_exit_futex -a -o "${temp_data}" \
		-- sleep 0.1 2>/dev/null; then
		echo "Skipping file mode test (record failed)"
		return
	fi

	if ! perf script futex-contention -i "${temp_data}" > "${temp_out}"; then
		echo "File mode test failed."
		err=1
	else
		echo "File mode test passed."
	fi
}

test_synthetic
test_file_mode

exit $err
