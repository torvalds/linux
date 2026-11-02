#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# stackcollapse python test

set -e -o pipefail

shelldir=$(dirname "$0")
# shellcheck source=lib/setup_python.sh
. "${shelldir}"/lib/setup_python.sh

if ! "$PYTHON" -c 'import perf' > /dev/null 2>&1; then
	echo "Skipping test, perf python module not found"
	exit 2
fi

script_dir="$(dirname "$0")/../../python"
script_path="${script_dir}/stackcollapse.py"

if [ ! -f "$script_path" ]; then
	echo "Skipping test, stackcollapse.py not found at $script_path"
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

echo "Testing stackcollapse.py..."

# Create a perf.data file with callchains. Use a busy workload rather than
# sleep, as an idle system may not generate any samples at all.
perf record -B -N --no-bpf-event -g -o "${temp_data}" \
		-- perf test -w noploop >/dev/null 2>&1 || \
		{ echo "Skipping test, perf record failed"; exit 2; }

if [ ! -s "${temp_data}" ]; then
	echo "Skipping test, perf record failed to create data"
	exit 2
fi

# Check that the script executes with default options
if ! perf script stackcollapse -i "${temp_data}" > "${temp_out}"; then
	echo "stackcollapse.py test failed"
	err=1
else
	# It outputs stacks like: swapper;...;... 2
	if [ ! -s "${temp_out}" ]; then
		echo "Expected stack traces in output, but output is empty."
		err=1
	else
		echo "stackcollapse default test passed."
	fi
fi

# Test CLI flags (--include-pid, --include-tid, --tidy-java, --kernel) and BrokenPipeError
if ! perf script stackcollapse -i "${temp_data}" \
	--include-pid --include-tid --tidy-java --kernel | head -n 1 > "${temp_out}" || \
   [ ! -s "${temp_out}" ]; then
	echo "stackcollapse.py options/pipe test failed"
	err=1
elif ! perf script stackcollapse -i "${temp_data}" --no-comm > "${temp_out}" || \
     [ ! -s "${temp_out}" ]; then
	echo "stackcollapse.py --no-comm test failed"
	err=1
else
	echo "stackcollapse options test passed."
fi
rm -f "${temp_out}"

exit $err
