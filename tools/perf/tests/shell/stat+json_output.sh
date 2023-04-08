#!/bin/bash
# perf stat JSON output linter
# SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
# Checks various perf stat JSON output commands for the
# correct number of fields.

set -e

skip_test=0

pythonchecker=$(dirname $0)/lib/perf_json_output_lint.py
if [ "x$PYTHON" == "x" ]
then
	if which python3 > /dev/null
	then
		PYTHON=python3
	elif which python > /dev/null
	then
		PYTHON=python
	else
		echo Skipping test, python not detected please set environment variable PYTHON.
		exit 2
	fi
fi

stat_output=$(mktemp /tmp/__perf_test.stat_output.json.XXXXX)

cleanup() {
  rm -f "${stat_output}"

  trap - EXIT TERM INT
}

trap_cleanup() {
  cleanup
  exit 1
}
trap trap_cleanup EXIT TERM INT

# Return true if perf_event_paranoid is > $1 and not running as root.
function ParanoidAndNotRoot()
{
	 [ $(id -u) != 0 ] && [ $(cat /proc/sys/kernel/perf_event_paranoid) -gt $1 ]
}

check_no_args()
{
	echo -n "Checking json output: no args "
	perf stat -j -o "${stat_output}" true
	$PYTHON $pythonchecker --no-args --file "${stat_output}"
	echo "[Success]"
}

check_system_wide()
{
	echo -n "Checking json output: system wide "
	if ParanoidAndNotRoot 0
	then
		echo "[Skip] paranoia and not root"
		return
	fi
	perf stat -j -a -o "${stat_output}" true
	$PYTHON $pythonchecker --system-wide --file "${stat_output}"
	echo "[Success]"
}

check_system_wide_no_aggr()
{
	echo -n "Checking json output: system wide no aggregation "
	if ParanoidAndNotRoot 0
	then
		echo "[Skip] paranoia and not root"
		return
	fi
	perf stat -j -A -a --no-merge -o "${stat_output}" true
	$PYTHON $pythonchecker --system-wide-no-aggr --file "${stat_output}"
	echo "[Success]"
}

check_interval()
{
	echo -n "Checking json output: interval "
	perf stat -j -I 1000 -o "${stat_output}" true
	$PYTHON $pythonchecker --interval --file "${stat_output}"
	echo "[Success]"
}


check_event()
{
	echo -n "Checking json output: event "
	perf stat -j -e cpu-clock -o "${stat_output}" true
	$PYTHON $pythonchecker --event --file "${stat_output}"
	echo "[Success]"
}

check_per_core()
{
	echo -n "Checking json output: per core "
	if ParanoidAndNotRoot 0
	then
		echo "[Skip] paranoia and not root"
		return
	fi
	perf stat -j --per-core -a -o "${stat_output}" true
	$PYTHON $pythonchecker --per-core --file "${stat_output}"
	echo "[Success]"
}

check_per_thread()
{
	echo -n "Checking json output: per thread "
	if ParanoidAndNotRoot 0
	then
		echo "[Skip] paranoia and not root"
		return
	fi
	perf stat -j --per-thread -a -o "${stat_output}" true
	$PYTHON $pythonchecker --per-thread --file "${stat_output}"
	echo "[Success]"
}

check_per_die()
{
	echo -n "Checking json output: per die "
	if ParanoidAndNotRoot 0
	then
		echo "[Skip] paranoia and not root"
		return
	fi
	perf stat -j --per-die -a -o "${stat_output}" true
	$PYTHON $pythonchecker --per-die --file "${stat_output}"
	echo "[Success]"
}

check_per_node()
{
	echo -n "Checking json output: per node "
	if ParanoidAndNotRoot 0
	then
		echo "[Skip] paranoia and not root"
		return
	fi
	perf stat -j --per-node -a -o "${stat_output}" true
	$PYTHON $pythonchecker --per-node --file "${stat_output}"
	echo "[Success]"
}

check_per_socket()
{
	echo -n "Checking json output: per socket "
	if ParanoidAndNotRoot 0
	then
		echo "[Skip] paranoia and not root"
		return
	fi
	perf stat -j --per-socket -a -o "${stat_output}" true
	$PYTHON $pythonchecker --per-socket --file "${stat_output}"
	echo "[Success]"
}

# The perf stat options for per-socket, per-core, per-die
# and -A ( no_aggr mode ) uses the info fetched from this
# directory: "/sys/devices/system/cpu/cpu*/topology". For
# example, socket value is fetched from "physical_package_id"
# file in topology directory.
# Reference: cpu__get_topology_int in util/cpumap.c
# If the platform doesn't expose topology information, values
# will be set to -1. For example, incase of pSeries platform
# of powerpc, value for  "physical_package_id" is restricted
# and set to -1. Check here validates the socket-id read from
# topology file before proceeding further

FILE_LOC="/sys/devices/system/cpu/cpu*/topology/"
FILE_NAME="physical_package_id"

check_for_topology()
{
	if ! ParanoidAndNotRoot 0
	then
		socket_file=`ls $FILE_LOC/$FILE_NAME | head -n 1`
		[ -z $socket_file ] && return 0
		socket_id=`cat $socket_file`
		[ $socket_id == -1 ] && skip_test=1
		return 0
	fi
}

check_for_topology
check_no_args
check_system_wide
check_interval
check_event
check_per_thread
check_per_node
if [ $skip_test -ne 1 ]
then
	check_system_wide_no_aggr
	check_per_core
	check_per_die
	check_per_socket
else
	echo "[Skip] Skipping tests for system_wide_no_aggr, per_core, per_die and per_socket since socket id exposed via topology is invalid"
fi
cleanup
exit 0
