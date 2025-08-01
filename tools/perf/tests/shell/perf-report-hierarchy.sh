#!/bin/bash
# perf report --hierarchy
# SPDX-License-Identifier: GPL-2.0
# Arnaldo Carvalho de Melo <acme@redhat.com> 

set -e

temp_dir=$(mktemp -d /tmp/perf-test-report.XXXXXXXXXX)

cleanup()
{
	trap - EXIT TERM INT
	sane=$(echo "${temp_dir}" | cut -b 1-21)
	if [ "${sane}" = "/tmp/perf-test-report" ] ; then
		echo "--- Cleaning up ---"
		rm -rf "${temp_dir:?}/"*
		rmdir "${temp_dir}"
	fi
}

trap_cleanup()
{
	cleanup
	exit 1
}

trap trap_cleanup EXIT TERM INT

test_report_hierarchy()
{
	echo "perf report --hierarchy"

	perf_data="${temp_dir}/perf-report-hierarchy-perf.data"
	perf record -o "${perf_data}" uname
	perf report --hierarchy -i "${perf_data}" > /dev/null
	echo "perf report --hierarchy test [Success]"
}

test_report_hierarchy

cleanup

exit 0
