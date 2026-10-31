#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# sctop python test

set -e

shelldir=$(dirname "$0")
# shellcheck source=lib/setup_python.sh
. "${shelldir}"/lib/setup_python.sh

if ! "$PYTHON" -c 'import perf' > /dev/null 2>&1; then
	echo "Skipping test, perf python module not found"
	exit 2
fi

script_dir="$(dirname "$0")/../../python"
script_path="${script_dir}/sctop.py"

if ! perf check feature -q libtraceevent > /dev/null 2>&1; then
	echo "Skipping test, libtraceevent is disabled"
	exit 2
fi

if [ ! -f "$script_path" ]; then
	echo "Skipping test, sctop.py not found at $script_path"
	exit 2
fi

err=0
temp_dir=$(mktemp -d /tmp/perf-sctop-XXXXXX)
temp_data="${temp_dir}/perf.data"
temp_out="${temp_dir}/perf.out"

cleanup() {
	rm -rf "${temp_dir}"
}
trap 'cleanup' EXIT TERM INT

echo "Testing sctop.py..."

# Create a perf.data file.
if ! perf list tracepoint | grep -q "raw_syscalls:sys_enter"; then
	echo "Skipping test, no raw_syscalls:sys_enter event"
	exit 2
fi

passed=0
for _ in 1 2 3 4 5; do
	rm -f "${temp_data}" "${temp_out}"
	if ! perf record -B -N --no-bpf-event -e raw_syscalls:sys_enter -o "${temp_data}" \
		-- sh -c "sleep 0.1; sleep 0.05" >/dev/null 2>&1; then
		echo "Skipping test, perf record failed"
		exit 2
	fi

	if [ ! -s "${temp_data}" ]; then
		continue
	fi

	# Check that the script executes
	if perf script sctop -i "${temp_data}" > "${temp_out}" && \
	   grep -E -q "[0-9]+$" "${temp_out}" && \
	   perf script sctop -i "${temp_data}" sleep 1 > "${temp_out}" && \
	   grep -E -q "[0-9]+$" "${temp_out}"; then
		passed=1
		break
	fi
done

if [ "$passed" -eq 0 ]; then
	echo "sctop.py test failed"
	err=1
else
	echo "sctop test passed."
fi
rm -f "${temp_out}"

exit $err
