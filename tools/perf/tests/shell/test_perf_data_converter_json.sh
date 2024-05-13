#!/bin/bash
# 'perf data convert --to-json' command test
# SPDX-License-Identifier: GPL-2.0

set -e

err=0

if [ "$PYTHON" = "" ] ; then
	if which python3 > /dev/null ; then
		PYTHON=python3
	elif which python > /dev/null ; then
		PYTHON=python
	else
		echo Skipping test, python not detected please set environment variable PYTHON.
		exit 2
	fi
fi

perfdata=$(mktemp /tmp/__perf_test.perf.data.XXXXX)
result=$(mktemp /tmp/__perf_test.output.json.XXXXX)

cleanup()
{
	rm -f "${perfdata}"
	rm -f "${result}"
	trap - exit term int
}

trap_cleanup()
{
	cleanup
	exit ${err}
}
trap trap_cleanup exit term int

test_json_converter_command()
{
	echo "Testing Perf Data Convertion Command to JSON"
	perf record -o "$perfdata" -F 99 -g -- perf test -w noploop > /dev/null 2>&1
	perf data convert --to-json "$result" --force -i "$perfdata" >/dev/null 2>&1
	if [ "$(cat ${result} | wc -l)" -gt "0" ] ; then
		echo "Perf Data Converter Command to JSON [SUCCESS]"
	else
		echo "Perf Data Converter Command to JSON [FAILED]"
		err=1
		exit
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
			exit
		fi
	else
		echo "File not found [FAILED]"
		err=2
		exit
	fi
}

test_json_converter_command
validate_json_format

exit ${err}
