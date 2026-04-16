#!/bin/bash
# 'perf data convert --to-json' command test
# SPDX-License-Identifier: GPL-2.0

set -e

err=0

shelldir=$(dirname "$0")
# shellcheck source=lib/setup_python.sh
. "${shelldir}"/lib/setup_python.sh

perfdata=$(mktemp /tmp/__perf_test.perf.data.XXXXX)
result=$(mktemp /tmp/__perf_test.output.json.XXXXX)

cleanup()
{
	rm -f "${perfdata}*"
	rm -f "${result}"
	trap - exit term int
}

trap_cleanup()
{
	echo "Unexpected signal in ${FUNCNAME[1]}"
	cleanup
	exit 1
}
trap trap_cleanup exit term int

test_json_converter_command()
{
	echo "Testing Perf Data Conversion Command to JSON"
	perf record -o "$perfdata" -F 99 -g -- perf test -w noploop
	perf data convert --to-json "$result" --force -i "$perfdata"
	if [ "$(cat ${result} | wc -l)" -gt "0" ] ; then
		echo "Perf Data Converter Command to JSON [SUCCESS]"
	else
		echo "Perf Data Converter Command to JSON [FAILED]"
		err=1
	fi
}

test_json_converter_pipe()
{
	echo "Testing Perf Data Conversion Command to JSON (Pipe mode)"
	perf record -o - -F 99 -g -- perf test -w noploop > "$perfdata"
	cat "$perfdata" | perf data convert --to-json "$result" --force -i -
	if [ "$(cat ${result} | wc -l)" -gt "0" ] ; then
		echo "Perf Data Converter Command to JSON (Pipe mode) [SUCCESS]"
	else
		echo "Perf Data Converter Command to JSON (Pipe mode) [FAILED]"
		err=1
	fi
}

validate_json_format()
{
	echo "Validating Perf Data Converted JSON file"
	if [ -f "$result" ] ; then
		if $PYTHON -c  "import json; json.load(open('$result'))" >/dev/null 2>&1 ; then
			echo "The file contains valid JSON format [SUCCESS]"
		else
			echo "The file does not contain valid JSON format [FAILED]"
			err=1
		fi
	else
		echo "File not found [FAILED]"
		err=1
	fi
}

test_json_converter_command
validate_json_format

test_json_converter_pipe
validate_json_format

cleanup
exit ${err}
