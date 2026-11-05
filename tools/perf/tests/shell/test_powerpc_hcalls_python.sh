#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# powerpc-hcalls python test

set -e

shelldir=$(dirname "$0")
# shellcheck source=lib/setup_python.sh
. "${shelldir}"/lib/setup_python.sh

if ! "$PYTHON" -c 'import perf' > /dev/null 2>&1; then
	echo "Skipping test, perf python module not found"
	exit 2
fi

script_dir="$(dirname "$0")/../../python"
script_path="${script_dir}/powerpc-hcalls.py"

if ! perf check feature -q libtraceevent > /dev/null 2>&1; then
	echo "Skipping test, libtraceevent is disabled"
	exit 2
fi

if [ ! -f "$script_path" ]; then
	echo "Skipping test, powerpc-hcalls.py not found at $script_path"
	exit 2
fi

err=0
cleanup() {
	rm -f "${temp_data}" "${temp_out}"
}
trap 'cleanup' EXIT TERM INT

temp_data=$(mktemp /tmp/perf.data.XXXXXX)
temp_out=$(mktemp /tmp/perf.out.XXXXXX)

echo "Testing powerpc-hcalls.py..."

# Verify HCallAnalyzer aggregation logic on synthetic hcall_entry/hcall_exit samples
if ! "$PYTHON" - "$script_path" > "${temp_out}" <<'EOF'
import argparse
import importlib.util
import sys
from types import SimpleNamespace

spec = importlib.util.spec_from_file_location("powerpc_hcalls", sys.argv[1])
assert spec and spec.loader
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

analyzer = mod.HCallAnalyzer(argparse.Namespace(sort='count'))
analyzer.process_event(SimpleNamespace(
    evsel="evsel(powerpc:hcall_entry)", sample_cpu=0, sample_time=1000, opcode=4))
analyzer.process_event(SimpleNamespace(
    evsel="evsel(powerpc:hcall_exit)", sample_cpu=0, sample_time=2500, opcode=4))
analyzer.print_summary()
EOF
then
	echo "powerpc-hcalls.py synthetic aggregation test failed"
	exit 1
fi

if ! grep -q "H_REMOVE.*1.*1500.*1500.*1500" "${temp_out}"; then
	echo "Failed to find aggregated H_REMOVE metrics in output"
	exit 1
fi

# Create a perf.data file if powerpc hcall tracepoints are available on this host.
if ! perf record -B -N --no-bpf-event -e powerpc:hcall_entry,powerpc:hcall_exit \
	-a -o "${temp_data}" -- perf test -w noploop >/dev/null 2>&1; then
	echo "Skipping live record test, powerpc hcall tracepoints not available"
	exit 0
fi

if [ ! -s "${temp_data}" ]; then
	echo "Skipping live record test, perf record failed to create data"
	exit 0
fi

# Check that the script executes on recorded perf.data
if ! perf script powerpc-hcalls -i "${temp_data}" > "${temp_out}"; then
	echo "powerpc-hcalls.py test failed"
	err=1
else
	if ! grep -q "hcall.*count.*min.*max.*avg" "${temp_out}"; then
		echo "Failed to find the metrics table header"
		err=1
	else
		echo "powerpc-hcalls test passed."
	fi
fi
rm -f "${temp_out}"

exit $err
