# SPDX-License-Identifier: GPL-2.0

# Return true if perf_event_paranoid is > $1 and not running as root.
function ParanoidAndNotRoot()
{
	 [ "$(id -u)" != 0 ] && [ "$(cat /proc/sys/kernel/perf_event_paranoid)" -gt $1 ]
}

# $1 name $2 extra_opt
check_no_args()
{
        echo -n "Checking $1 output: no args"
        perf stat $2 true
        commachecker --no-args
        echo "[Success]"
}

check_system_wide()
{
	echo -n "Checking $1 output: system wide "
	if ParanoidAndNotRoot 0
	then
		echo "[Skip] paranoid and not root"
		return
	fi
	perf stat -a $2 true
	commachecker --system-wide
	echo "[Success]"
}

check_system_wide_no_aggr()
{
	echo -n "Checking $1 output: system wide no aggregation "
	if ParanoidAndNotRoot 0
	then
		echo "[Skip] paranoid and not root"
		return
	fi
	perf stat -A -a --no-merge $2 true
	commachecker --system-wide-no-aggr
	echo "[Success]"
}

check_interval()
{
	echo -n "Checking $1 output: interval "
	perf stat -I 1000 $2 true
	commachecker --interval
	echo "[Success]"
}

check_event()
{
	echo -n "Checking $1 output: event "
	perf stat -e cpu-clock $2 true
	commachecker --event
	echo "[Success]"
}

check_per_core()
{
	echo -n "Checking $1 output: per core "
	if ParanoidAndNotRoot 0
	then
		echo "[Skip] paranoid and not root"
		return
	fi
	perf stat --per-core -a $2 true
	commachecker --per-core
	echo "[Success]"
}

check_per_thread()
{
	echo -n "Checking $1 output: per thread "
	if ParanoidAndNotRoot 0
	then
		echo "[Skip] paranoid and not root"
		return
	fi
	perf stat --per-thread -a $2 true
	commachecker --per-thread
	echo "[Success]"
}

check_per_cache_instance()
{
	echo -n "Checking $1 output: per cache instance "
	if ParanoidAndNotRoot 0
	then
		echo "[Skip] paranoid and not root"
		return
	fi
	perf stat --per-cache -a $2 true
	commachecker --per-cache
	echo "[Success]"
}

check_per_die()
{
	echo -n "Checking $1 output: per die "
	if ParanoidAndNotRoot 0
	then
		echo "[Skip] paranoid and not root"
		return
	fi
	perf stat --per-die -a $2 true
	commachecker --per-die
	echo "[Success]"
}

check_per_node()
{
	echo -n "Checking $1 output: per node "
	if ParanoidAndNotRoot 0
	then
		echo "[Skip] paranoid and not root"
		return
	fi
	perf stat --per-node -a $2 true
	commachecker --per-node
	echo "[Success]"
}

check_per_socket()
{
	echo -n "Checking $1 output: per socket "
	if ParanoidAndNotRoot 0
	then
		echo "[Skip] paranoid and not root"
		return
	fi
	perf stat --per-socket -a $2 true
	commachecker --per-socket
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

function check_for_topology()
{
	if ! ParanoidAndNotRoot 0
	then
		socket_file=`ls $FILE_LOC/$FILE_NAME | head -n 1`
		[ -z $socket_file ] && {
			echo 0
			return
		}
		socket_id=`cat $socket_file`
		[ $socket_id == -1 ] && {
			echo 1
			return
		}
	fi
	echo 0
}
