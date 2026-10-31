#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# netdev_times python test

set -e

shelldir=$(dirname "$0")
# shellcheck source=lib/setup_python.sh
. "${shelldir}"/lib/setup_python.sh

if ! "$PYTHON" -c 'import perf' > /dev/null 2>&1; then
	echo "Skipping test, perf python module not found"
	exit 2
fi

script_dir="$(dirname "$0")/../../python"
script_path="${script_dir}/netdev-times.py"

if ! perf check feature -q libtraceevent > /dev/null 2>&1; then
	echo "Skipping test, libtraceevent is disabled"
	exit 2
fi

if [ ! -f "$script_path" ]; then
	echo "Skipping test, netdev-times.py not found at $script_path"
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

echo "Testing netdev-times.py..."

if ! "$PYTHON" - "$script_path" << 'EOF'
import argparse
import contextlib
import importlib.util
import io
import sys

spec = importlib.util.spec_from_file_location("netdev_times", sys.argv[1])
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

cfg = argparse.Namespace(tx=True, rx=False, dev=None, debug=False, input="perf.data")
analyzer = mod.NetdevTimes(cfg)
analyzer.handle_net_dev_queue({
    "time": 1_000_000_000,
    "skbaddr": 0xdeadbeef,
    "skblen": 1500,
    "dev_name": "eth0",
})
analyzer.handle_net_dev_xmit({
    "time": 1_000_500_000,
    "skbaddr": 0xdeadbeef,
    "rc": 0,
})
analyzer.handle_kfree_skb({
    "time": 1_000_750_000,
    "skbaddr": 0xdeadbeef,
    "comm": "ping",
    "pid": 42,
    "location": 0xffffffff81000000,
})

buf = io.StringIO()
with contextlib.redirect_stdout(buf):
    analyzer.print_summary()
out = buf.getvalue()
assert "eth0" in out and "1500" in out and "1.000000sec" in out, out
assert "0.500msec" in out and "0.250msec" in out, out
EOF
then
	echo "netdev-times.py synthetic unit test failed"
	exit 1
fi

# Create a perf.data file. Force dropping a packet if tracepoint is available!
if ! perf record -B -N --no-bpf-event -e skb:kfree_skb -a -o "${temp_data}" \
	-- ping -c 1 -W 1 127.0.0.1 >/dev/null 2>&1; then
	perf record -B -N --no-bpf-event -e cycles -o "${temp_data}" \
		-- perf test -w noploop >/dev/null 2>&1 || \
		{ echo "Skipping test, perf record failed"; exit 2; }
fi

# Check that the script executes
if ! perf script netdev-times -i "${temp_data}" > "${temp_out}"; then
	echo "netdev-times.py test failed"
	err=1
else
	echo "netdev-times test passed."
fi
rm -f "${temp_out}"

exit $err
