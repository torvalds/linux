#!/bin/bash
# Use vfs_getname probe to get syscall args filenames (exclusive)

# Uses the 'perf test shell' library to add probe:vfs_getname to the system
# then use it with 'perf record' using 'touch' to write to a temp file, then
# checks that that was captured by the vfs_getname probe in the generated
# perf.data file, with the temp file name as the pathname argument.

# SPDX-License-Identifier: GPL-2.0
# Arnaldo Carvalho de Melo <acme@kernel.org>, 2017

# shellcheck source=lib/probe.sh
. "$(dirname "$0")/lib/probe.sh"

skip_if_no_perf_probe || exit 2
[ "$(id -u)" = 0 ] || exit 2

# shellcheck source=lib/probe_vfs_getname.sh
. "$(dirname "$0")/lib/probe_vfs_getname.sh"

record_open_file() {
	echo "Recording open file:"
	# Check presence of libtraceevent support to run perf record
	skip_no_probe_record_support "probe:vfs_getname*"
	if [ $? -eq 2 ]; then
		echo "WARN: Skipping test record_open_file. No libtraceevent support"
		return 2
	fi
	perf record -o ${perfdata} -e probe:vfs_getname\* touch $file
}

perf_script_filenames() {
	echo "Looking at perf.data file for vfs_getname records for the file we touched:"
	perf script -i ${perfdata} | \
	grep -E " +touch +[0-9]+ +\[[0-9]+\] +[0-9]+\.[0-9]+: +probe:vfs_getname[_0-9]*: +\([[:xdigit:]]+\) +pathname=\"${file}\""
}

add_probe_vfs_getname
err=$?

if [ $err -eq 1 ] ; then
        skip_if_no_debuginfo
        err=$?
fi

if [ $err -ne 0 ] ; then
	exit $err
fi

perfdata=$(mktemp /tmp/__perf_test.perf.data.XXXXX)
file=$(mktemp /tmp/temporary_file.XXXXX)

record_open_file && perf_script_filenames
err=$?
rm -f ${perfdata}
rm -f ${file}
cleanup_probe_vfs_getname
exit $err
