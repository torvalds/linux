#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# mem-phys-addr python test

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
script_path="${script_dir}/mem-phys-addr.py"

if [ ! -f "$script_path" ]; then
	echo "Skipping test, mem-phys-addr.py not found at $script_path"
	exit 2
fi

err=0
temp_data=""
temp_iomem=""
temp_out=""

cleanup() {
	rm -f "${temp_data}" "${temp_iomem}" "${temp_out}"
}

trap 'cleanup' EXIT TERM INT

temp_data=$(mktemp /tmp/perf.data.XXXXXX)
temp_iomem=$(mktemp /tmp/perf.iomem.XXXXXX)
temp_out=$(mktemp /tmp/perf.out.XXXXXX)

cat << 'EOF' > "${temp_iomem}"
00000000-ffffffffffffffff : System RAM
  00000000-7fffffffffffffff : Low RAM
    00001000-00ffffff : Kernel code
  8000000000000000-ffffffffffffffff : High RAM
EOF

test_iomem_hierarchy() {
	echo "Testing mem-phys-addr.py hierarchical iomem resolution..."
	"$PYTHON" - "$script_path" "${temp_iomem}" << 'PYEOF' > "${temp_out}"
import importlib.util
import sys

spec = importlib.util.spec_from_file_location("mem_phys_addr", sys.argv[1])
mod = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = mod
spec.loader.exec_module(mod)

mod.parse_iomem(sys.argv[2])
entry_kernel = mod.find_memory_type(0x100000)
entry_high = mod.find_memory_type(0x9000000000000000)
assert entry_kernel is not None and entry_kernel.label == "Kernel code"
assert entry_high is not None and entry_high.label == "High RAM"
mod.event_counts["cpu/mem-loads/"][entry_kernel] += 3
mod.event_counts["cpu/mem-loads/"][entry_high] += 1
mod.print_memory_type()
PYEOF
	if ! grep -q "System RAM" "${temp_out}" || \
	   ! grep -q "Kernel code" "${temp_out}" || \
	   ! grep -q "High RAM" "${temp_out}"; then
		echo "Hierarchical iomem resolution test failed."
		err=1
	else
		echo "Hierarchical iomem resolution test passed."
	fi
}

test_file_mode() {
	echo "Testing mem-phys-addr.py file mode..."

	# Generate memory access events (try unprivileged user-space first, then system-wide)
	if ! perf record -B -N --no-bpf-event --phys-data -d -o "${temp_data}" \
	     -- perf test -w datasym >/dev/null 2>&1 && \
	   ! perf record -B -N --no-bpf-event -d -o "${temp_data}" \
	     -- perf test -w datasym >/dev/null 2>&1 && \
	   ! perf record -B -N --no-bpf-event -d -a -o "${temp_data}" \
	     -- sleep 0.1 >/dev/null 2>&1; then
		echo "Skipping file mode record test, perf record -d not supported"
		return 0
	fi

	# Run the script with custom --iomem
	if ! perf script mem-phys-addr -i "${temp_data}" \
		--iomem "${temp_iomem}" > "${temp_out}" || \
	   [ ! -s "${temp_out}" ]; then
		echo "File mode test failed."
		err=1
	else
		echo "File mode test passed."
	fi
}

test_iomem_hierarchy
test_file_mode

exit $err
