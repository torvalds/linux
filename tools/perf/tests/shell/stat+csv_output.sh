#!/bin/bash
# perf stat CSV output linter
# SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
# Tests various perf stat CSV output commands for the
# correct number of fields and the CSV separator set to ','.

set -e

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
		x=$(echo $line | tr -d -c ',')
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
	perf stat -x, true 2>&1 | commachecker --no-args
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
	perf stat -x, -a true 2>&1 | commachecker --system-wide
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
	perf stat -x, -A -a --no-merge true 2>&1 | commachecker --system-wide-no-aggr
	echo "[Success]"
}

check_interval()
{
	echo -n "Checking CSV output: interval "
	perf stat -x, -I 1000 true 2>&1 | commachecker --interval
	echo "[Success]"
}


check_event()
{
	echo -n "Checking CSV output: event "
	perf stat -x, -e cpu-clock true 2>&1 | commachecker --event
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
	perf stat -x, --per-core -a true 2>&1 | commachecker --per-core
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
	perf stat -x, --per-thread -a true 2>&1 | commachecker --per-thread
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
	perf stat -x, --per-die -a true 2>&1 | commachecker --per-die
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
	perf stat -x, --per-node -a true 2>&1 | commachecker --per-node
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
	perf stat -x, --per-socket -a true 2>&1 | commachecker --per-socket
	echo "[Success]"
}

check_no_args
check_system_wide
check_system_wide_no_aggr
check_interval
check_event
check_per_core
check_per_thread
check_per_die
check_per_node
check_per_socket
exit 0
