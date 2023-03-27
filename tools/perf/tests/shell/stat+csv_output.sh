#!/bin/bash
# perf stat CSV output linter
# SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
# Tests various perf stat CSV output commands for the
# correct number of fields and the CSV separator set to ','.

set -e

skip_test=0
csv_sep=@

function commachecker()
{
	local -i cnt=0
	local exp=0

	case "$1"
	in "--no-args")		exp=6
	;; "--system-wide")	exp=6
	;; "--event")		exp=6
	;; "--interval")	exp=7
	;; "--per-thread")	exp=7
	;; "--system-wide-no-aggr")	exp=7
				[ $(uname -m) = "s390x" ] && exp='^[6-7]$'
	;; "--per-core")	exp=8
	;; "--per-socket")	exp=8
	;; "--per-node")	exp=8
	;; "--per-die")		exp=8
	esac

	while read line
	do
		# Check for lines beginning with Failed
		x=${line:0:6}
		[ "$x" = "Failed" ] && continue

		# Count the number of commas
		x=$(echo $line | tr -d -c $csv_sep)
		cnt="${#x}"
		# echo $line $cnt
		[[ ! "$cnt" =~ $exp ]] && {
			echo "wrong number of fields. expected $exp in $line" 1>&2
			exit 1;
		}
	done
	return 0
}

# Return true if perf_event_paranoid is > $1 and not running as root.
function ParanoidAndNotRoot()
{
	 [ $(id -u) != 0 ] && [ $(cat /proc/sys/kernel/perf_event_paranoid) -gt $1 ]
}

check_no_args()
{
	echo -n "Checking CSV output: no args "
	perf stat -x$csv_sep true 2>&1 | commachecker --no-args
	echo "[Success]"
}

check_system_wide()
{
	echo -n "Checking CSV output: system wide "
	if ParanoidAndNotRoot 0
	then
		echo "[Skip] paranoid and not root"
		return
	fi
	perf stat -x$csv_sep -a true 2>&1 | commachecker --system-wide
	echo "[Success]"
}

check_system_wide_no_aggr()
{
	echo -n "Checking CSV output: system wide "
	if ParanoidAndNotRoot 0
	then
		echo "[Skip] paranoid and not root"
		return
	fi
	echo -n "Checking CSV output: system wide no aggregation "
	perf stat -x$csv_sep -A -a --no-merge true 2>&1 | commachecker --system-wide-no-aggr
	echo "[Success]"
}

check_interval()
{
	echo -n "Checking CSV output: interval "
	perf stat -x$csv_sep -I 1000 true 2>&1 | commachecker --interval
	echo "[Success]"
}


check_event()
{
	echo -n "Checking CSV output: event "
	perf stat -x$csv_sep -e cpu-clock true 2>&1 | commachecker --event
	echo "[Success]"
}

check_per_core()
{
	echo -n "Checking CSV output: per core "
	if ParanoidAndNotRoot 0
	then
		echo "[Skip] paranoid and not root"
		return
	fi
	perf stat -x$csv_sep --per-core -a true 2>&1 | commachecker --per-core
	echo "[Success]"
}

check_per_thread()
{
	echo -n "Checking CSV output: per thread "
	if ParanoidAndNotRoot 0
	then
		echo "[Skip] paranoid and not root"
		return
	fi
	perf stat -x$csv_sep --per-thread -a true 2>&1 | commachecker --per-thread
	echo "[Success]"
}

check_per_die()
{
	echo -n "Checking CSV output: per die "
	if ParanoidAndNotRoot 0
	then
		echo "[Skip] paranoid and not root"
		return
	fi
	perf stat -x$csv_sep --per-die -a true 2>&1 | commachecker --per-die
	echo "[Success]"
}

check_per_node()
{
	echo -n "Checking CSV output: per node "
	if ParanoidAndNotRoot 0
	then
		echo "[Skip] paranoid and not root"
		return
	fi
	perf stat -x$csv_sep --per-node -a true 2>&1 | commachecker --per-node
	echo "[Success]"
}

check_per_socket()
{
	echo -n "Checking CSV output: per socket "
	if ParanoidAndNotRoot 0
	then
		echo "[Skip] paranoid and not root"
		return
	fi
	perf stat -x$csv_sep --per-socket -a true 2>&1 | commachecker --per-socket
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
exit 0
