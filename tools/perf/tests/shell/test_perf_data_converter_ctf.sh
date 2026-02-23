#!/bin/bash
# 'perf data convert --to-ctf' command test
# SPDX-License-Identifier: GPL-2.0

set -e

err=0

perfdata=$(mktemp /tmp/__perf_test.perf.data.XXXXX)
ctf_dir=$(mktemp -d /tmp/__perf_test.ctf.XXXXX)

cleanup()
{
	rm -f "${perfdata}"
	rm -rf "${ctf_dir}"
	trap - exit term int
}

trap_cleanup()
{
	echo "Unexpected signal in ${FUNCNAME[1]}"
	cleanup
	exit ${err}
}
trap trap_cleanup exit term int

check_babeltrace_support()
{
	if ! perf check feature libbabeltrace
	then
		echo "perf not linked with libbabeltrace, skipping test"
		exit 2
	fi
}

test_ctf_converter_file()
{
	echo "Testing Perf Data Conversion Command to CTF (File input)"
	# Record some data
	if ! perf record -o "$perfdata" -F 99 -g -- perf test -w noploop
	then
		echo "Failed to record perf data"
		err=1
		return
	fi

	# Cleanup previous ctf dir
	rm -rf "${ctf_dir}"

	# Convert
	if ! perf data convert --to-ctf "$ctf_dir" --force -i "$perfdata"
	then
		echo "Perf Data Converter Command to CTF (File input) [FAILED]"
		err=1
		return
	fi

	if [ -d "${ctf_dir}" ] && [ "$(ls -A "${ctf_dir}")" ]
	then
		echo "Perf Data Converter Command to CTF (File input) [SUCCESS]"
	else
		echo "Perf Data Converter Command to CTF (File input) [FAILED]"
		echo "    Output directory empty or missing"
		err=1
	fi
}

test_ctf_converter_pipe()
{
	echo "Testing Perf Data Conversion Command to CTF (Pipe mode)"

	# Cleanup previous ctf dir
	rm -rf "${ctf_dir}"

	# Record to stdout and pipe to $perfdata file
	if ! perf record -o - -F 99 -g -- perf test -w noploop > "$perfdata"
	then
		echo "Failed to record perf data"
		err=1
		return
	fi

	if ! perf data convert --to-ctf "$ctf_dir" --force -i "$perfdata"
	then
		echo "Perf Data Converter Command to CTF (Pipe mode) [FAILED]"
		err=1
		return
	fi

	if [ -d "${ctf_dir}" ] && [ "$(ls -A "${ctf_dir}")" ]
	then
		echo "Perf Data Converter Command to CTF (Pipe mode) [SUCCESS]"
	else
		echo "Perf Data Converter Command to CTF (Pipe mode) [FAILED]"
		echo "    Output directory empty or missing"
		err=1
	fi
}

check_babeltrace_support
test_ctf_converter_file
test_ctf_converter_pipe

exit ${err}
