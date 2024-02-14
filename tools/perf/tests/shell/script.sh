#!/bin/sh
# perf script tests
# SPDX-License-Identifier: GPL-2.0

set -e

temp_dir=$(mktemp -d /tmp/perf-test-script.XXXXXXXXXX)

perfdatafile="${temp_dir}/perf.data"
db_test="${temp_dir}/db_test.py"

err=0

cleanup()
{
	trap - EXIT TERM INT
	sane=$(echo "${temp_dir}" | cut -b 1-21)
	if [ "${sane}" = "/tmp/perf-test-script" ] ; then
		echo "--- Cleaning up ---"
		rm -f "${temp_dir}/"*
		rmdir "${temp_dir}"
	fi
}

trap_cleanup()
{
	cleanup
	exit 1
}

trap trap_cleanup EXIT TERM INT


test_db()
{
	echo "DB test"

	# Check if python script is supported
        if perf version --build-options | grep python | grep -q OFF ; then
		echo "SKIP: python scripting is not supported"
		err=2
		return
	fi

	cat << "_end_of_file_" > "${db_test}"
perf_db_export_mode = True
perf_db_export_calls = False
perf_db_export_callchains = True

def sample_table(*args):
    print(f'sample_table({args})')

def call_path_table(*args):
    print(f'call_path_table({args}')
_end_of_file_
	case $(uname -m)
	in s390x)
		cmd_flags="--call-graph dwarf -e cpu-clock";;
	*)
		cmd_flags="-g";;
	esac

	perf record $cmd_flags -o "${perfdatafile}" true
	perf script -i "${perfdatafile}" -s "${db_test}"
	echo "DB test [Success]"
}

test_db

cleanup

exit $err
