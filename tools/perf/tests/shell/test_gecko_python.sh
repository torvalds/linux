#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# gecko python test

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
script_path="${script_dir}/gecko.py"

if [ ! -f "$script_path" ]; then
	echo "Skipping test, gecko.py not found at $script_path"
	exit 2
fi

err=0
temp_data=""
temp_json=""

cleanup() {
	rm -f "${temp_data}" "${temp_json}"
}

trap 'cleanup' EXIT TERM INT

temp_data=$(mktemp /tmp/perf.data.XXXXXX)
temp_json=$(mktemp /tmp/perf.gecko.json.XXXXXX)

test_file_mode() {
	echo "Testing gecko.py..."

	# Generate some events with callchains
	if ! perf record -B -N --no-bpf-event -g -o "${temp_data}" \
		-- perf test -w noploop >/dev/null 2>&1; then
		echo "Skipping test, perf record -g failed (permissions or lack of support)"
		exit 2
	fi

	# Run the script in save-only mode with custom product and category colors
	if ! perf script gecko -i "${temp_data}" \
		--product "perf-test-product" --user-color blue --kernel-color red \
		--save-only "${temp_json}" >/dev/null; then
		echo "File mode test failed."
		err=1
	else
		# Validate JSON schema and custom CLI option values
		if ! "$PYTHON" - "$script_path" "${temp_json}" << 'PYEOF'
import importlib.util
import json
import sys

with open(sys.argv[2], encoding="utf-8") as f:
    data = json.load(f)

assert data["meta"]["product"] == "perf-test-product"
assert data["meta"]["version"] == 24
assert isinstance(data["threads"], list) and len(data["threads"]) > 0

spec = importlib.util.spec_from_file_location("gecko", sys.argv[1])
mod = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = mod
spec.loader.exec_module(mod)

class DummyHandler:
    headers = []
    error_code = None
    def send_header(self, k, v):
        self.headers.append((k, v))
    def send_error(self, code, _msg):
        self.error_code = code

dummy = DummyHandler()
mod.CORSRequestHandler.list_directory(dummy, "/tmp")
assert dummy.error_code == 403
PYEOF
		then
			echo "Gecko JSON and CORSRequestHandler validation failed."
			err=1
		else
			echo "File mode JSON and CORSRequestHandler test passed."
		fi
	fi
}

test_file_mode

exit $err
