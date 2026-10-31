#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# net_dropmonitor python test

set -e

shelldir=$(dirname "$0")
# shellcheck source=lib/setup_python.sh
. "${shelldir}"/lib/setup_python.sh

if ! "$PYTHON" -c 'import perf' > /dev/null 2>&1; then
	echo "Skipping test, perf python module not found"
	exit 2
fi

script_dir="$(dirname "$0")/../../python"
script_path="${script_dir}/net_dropmonitor.py"

if ! perf check feature -q libtraceevent > /dev/null 2>&1; then
	echo "Skipping test, libtraceevent is disabled"
	exit 2
fi

if [ ! -f "$script_path" ]; then
	echo "Skipping test, net_dropmonitor.py not found at $script_path"
	exit 2
fi

err=0
temp_data=""
temp_out=""

cleanup() {
	rm -f "${temp_data}" "${temp_out}"
}
trap 'cleanup' EXIT
trap 'cleanup; exit 1' TERM INT

temp_data=$(mktemp /tmp/perf.data.XXXXXX)
temp_out=$(mktemp /tmp/perf.out.XXXXXX)

echo "Testing net_dropmonitor.py..."

# Create a perf.data file. Force dropping a packet if tracepoint is available!
if ! perf record -B -N --no-bpf-event -e skb:kfree_skb -o "${temp_data}" -a \
	-- ping -c 1 -W 1 255.255.255.255 >/dev/null 2>&1; then
	if ! perf record -B -N --no-bpf-event -e skb:kfree_skb -o "${temp_data}" \
		-- sleep 0.1 >/dev/null 2>&1; then
		if ! perf record -B -N --no-bpf-event -o "${temp_data}" \
			-- uname >/dev/null 2>&1; then
			echo "Skipping test, cannot record perf events"
			exit 2
		fi
	fi
fi

if [ ! -s "${temp_data}" ]; then
	echo "Skipping test, perf record failed to create data"
	exit 2
fi

# Check that the script executes and outputs table header
if ! perf script net_dropmonitor -i "${temp_data}" > "${temp_out}"; then
	echo "net_dropmonitor.py test failed"
	err=1
else
	if ! grep -q "LOCATION.*OFFSET.*COUNT" "${temp_out}"; then
		echo "Failed to find the metrics table header"
		err=1
	fi
fi

# Verify DropMonitor event processing and symbol resolution
if [ $err -eq 0 ]; then
	if ! "$PYTHON" -c "
import sys
sys.path.insert(0, sys.argv[1])
import net_dropmonitor

class DummySample:
    evsel = 'skb:kfree_skb'
    location = 0xffffffff81001010
    sample_ip = 0xffffffff81001010
    symbol = 'ip_rcv_finish'
    sym_offset = 16
    callchain = []

dm = net_dropmonitor.DropMonitor()
dm.process_event(DummySample())
dm.print_drop_table()
" "${script_dir}" > "${temp_out}"; then
		echo "net_dropmonitor.py unit test failed"
		err=1
	elif ! grep -q "ip_rcv_finish.*16.*1" "${temp_out}"; then
		echo "Failed to find expected symbol resolution in net_dropmonitor.py"
		err=1
	else
		echo "net_dropmonitor test passed."
	fi
fi
rm -f "${temp_out}"

exit $err
