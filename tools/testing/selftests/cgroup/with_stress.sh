#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

stress_fork()
{
	while true ; do
		/usr/bin/true
		sleep 0.01
	done
}

stress_subsys()
{
	local verb=+
	while true ; do
		echo $verb$subsys_ctrl >$sysfs/cgroup.subtree_control
		[ $verb = "+" ] && verb=- || verb=+
		# incommensurable period with other stresses
		sleep 0.011
	done
}

init_and_check()
{
	sysfs=`mount -t cgroup2 | head -1 | awk '{ print $3 }'`
	if [ ! -d "$sysfs" ]; then
		echo "Skipping: cgroup2 is not mounted" >&2
		exit $ksft_skip
	fi

	if ! echo +$subsys_ctrl >$sysfs/cgroup.subtree_control ; then
		echo "Skipping: cannot enable $subsys_ctrl in $sysfs" >&2
		exit $ksft_skip
	fi

	if ! echo -$subsys_ctrl >$sysfs/cgroup.subtree_control ; then
		echo "Skipping: cannot disable $subsys_ctrl in $sysfs" >&2
		exit $ksft_skip
	fi
}

declare -a stresses
declare -a stress_pids
duration=5
rc=0
subsys_ctrl=cpuset
sysfs=

while getopts c:d:hs: opt; do
	case $opt in
	c)
		subsys_ctrl=$OPTARG
		;;
	d)
		duration=$OPTARG
		;;
	h)
		echo "Usage $0 [ -s stress ] ... [ -d duration ] [-c controller] cmd args .."
		echo -e "\t default duration $duration seconds"
		echo -e "\t default controller $subsys_ctrl"
		exit
		;;
	s)
		func=stress_$OPTARG
		if [ "x$(type -t $func)" != "xfunction" ] ; then
			echo "Unknown stress $OPTARG"
			exit 1
		fi
		stresses+=($func)
		;;
	esac
done
shift $((OPTIND - 1))

init_and_check

for s in ${stresses[*]} ; do
	$s &
	stress_pids+=($!)
done


time=0
start=$(date +%s)

while [ $time -lt $duration ] ; do
	$*
	rc=$?
	[ $rc -eq 0 ] || break
	time=$(($(date +%s) - $start))
done

for pid in ${stress_pids[*]} ; do
	kill -SIGTERM $pid
	wait $pid
done

exit $rc
