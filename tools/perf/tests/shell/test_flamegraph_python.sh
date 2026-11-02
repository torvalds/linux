#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# flamegraph python test

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
script_path="${script_dir}/flamegraph.py"

if [ ! -f "$script_path" ]; then
	echo "Skipping test, flamegraph.py not found at $script_path"
	exit 2
fi

err=0
temp_data=""
temp_json=""
temp_html=""
temp_tpl=""

cleanup() {
	rm -f "${temp_data}" "${temp_json}" "${temp_html}" "${temp_tpl}"
}

trap 'cleanup' EXIT TERM INT

temp_data=$(mktemp /tmp/perf.data.XXXXXX)
temp_json=$(mktemp /tmp/perf.flamegraph.json.XXXXXX)
temp_html=$(mktemp /tmp/perf.flamegraph.html.XXXXXX)
temp_tpl=$(mktemp /tmp/perf.flamegraph.tpl.XXXXXX)

validate_json() {
	local json_file="$1"
	local label="$2"

	if ! "$PYTHON" -m json.tool "${json_file}" /dev/null >/dev/null 2>&1; then
		echo "${label} JSON validation failed."
		err=1
	elif ! "$PYTHON" -c "import json, sys
d = json.load(open(sys.argv[1]))
assert d.get('v', 0) > 0 or len(d.get('c', [])) > 0" "${json_file}" >/dev/null 2>&1; then
		echo "${label} JSON flamegraph structure is empty."
		err=1
	else
		echo "${label} JSON test passed."
	fi
}

test_file_mode() {
	echo "Testing flamegraph.py..."

	# Generate some events with callchains
	if ! perf record -B -N --no-bpf-event -g -o "${temp_data}" \
		-- perf test -w noploop >/dev/null 2>&1; then
		echo "Skipping test, perf record -g failed (permissions or lack of support)"
		exit 2
	fi

	# Run the script in file mode and validate JSON output
	if ! perf script flamegraph -i "${temp_data}" -f json -o "${temp_json}" >/dev/null; then
		echo "File mode JSON test failed."
		err=1
	else
		validate_json "${temp_json}" "File mode"
	fi

	# Run the script in pipe ('-') mode and validate JSON output
	rm -f "${temp_json}"
	if ! perf record -B -N --no-bpf-event -g -o - -- perf test -w noploop 2>/dev/null | \
		perf script flamegraph -i - -f json -o "${temp_json}" >/dev/null; then
		echo "Pipe stdin JSON mode test failed."
		err=1
	else
		validate_json "${temp_json}" "Pipe mode"
	fi

	# Run the script, dump as html to temp_html using MINIMAL_HTML fallback
	if ! perf script flamegraph -i "${temp_data}" -f html \
		--template /nonexistent/template.html -o "${temp_html}" >/dev/null 2>&1; then
		echo "File mode HTML test failed."
		err=1
	else
		if ! grep -q "<head>" "${temp_html}" || \
		   ! grep -q 'crossorigin="anonymous"' "${temp_html}"; then
			echo "HTML validation failed."
			err=1
		else
			echo "File mode HTML test passed."
		fi
	fi

	# Test custom local HTML template and --colorscheme option
	cat << 'EOF' > "${temp_tpl}"
<html><body><script>
const opts = /** @options_json **/;
const data = /** @flamegraph_json **/;
</script></body></html>
EOF
	if ! perf script flamegraph -i "${temp_data}" -f html --template "${temp_tpl}" \
		--colorscheme blue-green -o "${temp_html}" >/dev/null 2>&1; then
		echo "Custom template HTML test failed."
		err=1
	elif ! grep -q "blue-green" "${temp_html}"; then
		echo "Custom template colorscheme substitution failed."
		err=1
	else
		echo "Custom template HTML test passed."
	fi
}

test_file_mode

exit $err
