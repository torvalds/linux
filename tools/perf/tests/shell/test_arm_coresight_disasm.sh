#!/bin/bash
# Check Arm CoreSight disassembly script completes without errors (exclusive)
# SPDX-License-Identifier: GPL-2.0

# The disassembly script reconstructs ranges of instructions and gives these to objdump to
# decode. objdump doesn't like ranges that go backwards, but these are a good indication
# that decoding has gone wrong either in OpenCSD, Perf or in the range reconstruction in
# the script. Test all 3 parts are working correctly by running the script.

skip_if_no_cs_etm_event() {
	perf list pmu | grep -q 'cs_etm//' && return 0

	# cs_etm event doesn't exist
	return 2
}

skip_if_no_cs_etm_event || exit 2

# Assume an error unless we reach the very end
set -e
glb_err=1

perfdata_dir=$(mktemp -d /tmp/__perf_test.perf.data.XXXXX)
perfdata=${perfdata_dir}/perf.data
file=$(mktemp /tmp/temporary_file.XXXXX)
# Relative path works whether it's installed or running from repo
script_path=$(dirname "$0")/../../scripts/python/arm-cs-trace-disasm.py

cleanup_files()
{
	set +e
	rm -rf ${perfdata_dir}
	rm -f ${file}
	trap - EXIT TERM INT
	exit $glb_err
}

trap cleanup_files EXIT TERM INT

# Ranges start and end on branches, so check for some likely branch instructions
sep="\s\|\s"
branch_search="\sbl${sep}b${sep}b.ne${sep}b.eq${sep}cbz\s"

## Test kernel ##
if [ -e /proc/kcore ]; then
	echo "Testing kernel disassembly"
	perf record -o ${perfdata} -e cs_etm//k --kcore -- touch $file > /dev/null 2>&1
	perf script -i ${perfdata} -s python:${script_path} -- \
		-d --stop-sample=30 2> /dev/null > ${file}
	grep -q -e ${branch_search} ${file}
	echo "Found kernel branches"
else
	# kcore is required for correct kernel decode due to runtime code patching
	echo "No kcore, skipping kernel test"
fi

## Test user ##
echo "Testing userspace disassembly"
perf record -o ${perfdata} -e cs_etm//u -- touch $file > /dev/null 2>&1
perf script -i ${perfdata} -s python:${script_path} -- \
	-d --stop-sample=30 2> /dev/null > ${file}
grep -q -e ${branch_search} ${file}
echo "Found userspace branches"

glb_err=0
