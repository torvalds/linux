#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# perf test junit XML output validation

set -e

err=0

shelldir=$(dirname "$0")
# shellcheck source=lib/setup_python.sh
. "${shelldir}"/lib/setup_python.sh

result=$(mktemp /tmp/__perf_test.output.xml.XXXXX)

cleanup()
{
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

test_junit_output()
{
	echo "Testing perf test JUnit XML output command"
	perf test -v -j"$result" util || true
	if [ -s "$result" ] ; then
		echo "perf test JUnit XML output command [SUCCESS]"
	else
		echo "perf test JUnit XML output command [FAILED]"
		err=1
	fi
}

validate_xml_format()
{
	echo "Validating perf test converted JUnit XML file"
	if [ -f "$result" ] ; then
		if $PYTHON -c \
		    "import xml.etree.ElementTree as ET; ET.parse('$result')" \
		    >/dev/null 2>&1 ; then
			echo "The file contains valid XML format [SUCCESS]"
		else
			echo "The file does not contain valid XML format [FAILED]"
			err=1
		fi
	else
		echo "File not found [FAILED]"
		err=1
	fi
}

test_junit_output
validate_xml_format

cleanup
exit ${err}
