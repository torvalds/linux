#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

SYSFS=
# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4
retval=0

prerequisite()
{
	msg="skip all tests:"

	if [ $UID != 0 ]; then
		echo $msg must be run as root >&2
		exit $ksft_skip
	fi

	taskset -p 01 $$

	SYSFS=`mount -t sysfs | head -1 | awk '{ print $3 }'`

	if [ ! -d "$SYSFS" ]; then
		echo $msg sysfs is not mounted >&2
		exit $ksft_skip
	fi

	if ! ls $SYSFS/devices/system/cpu/cpu* > /dev/null 2>&1; then
		echo $msg cpu hotplug is not supported >&2
		exit $ksft_skip
	fi

	echo "CPU online/offline summary:"
	online_cpus=`cat $SYSFS/devices/system/cpu/online`
	online_max=${online_cpus##*-}

	if [[ "$online_cpus" = "$online_max" ]]; then
		echo "$msg: since there is only one cpu: $online_cpus"
		exit $ksft_skip
	fi

	present_cpus=`cat $SYSFS/devices/system/cpu/present`
	present_max=${present_cpus##*-}
	echo "present_cpus = $present_cpus present_max = $present_max"

	echo -e "\t Cpus in online state: $online_cpus"

	offline_cpus=`cat $SYSFS/devices/system/cpu/offline`
	if [[ "a$offline_cpus" = "a" ]]; then
		offline_cpus=0
	else
		offline_max=${offline_cpus##*-}
	fi
	echo -e "\t Cpus in offline state: $offline_cpus"
}

#
# list all hot-pluggable CPUs
#
hotpluggable_cpus()
{
	local state=${1:-.\*}

	for cpu in $SYSFS/devices/system/cpu/cpu*; do
		if [ -f $cpu/online ] && grep -q $state $cpu/online; then
			echo ${cpu##/*/cpu}
		fi
	done
}

hotpluggable_offline_cpus()
{
	hotpluggable_cpus 0
}

hotpluggable_online_cpus()
{
	hotpluggable_cpus 1
}

cpu_is_online()
{
	grep -q 1 $SYSFS/devices/system/cpu/cpu$1/online
}

cpu_is_offline()
{
	grep -q 0 $SYSFS/devices/system/cpu/cpu$1/online
}

online_cpu()
{
	echo 1 > $SYSFS/devices/system/cpu/cpu$1/online
}

offline_cpu()
{
	echo 0 > $SYSFS/devices/system/cpu/cpu$1/online
}

online_cpu_expect_success()
{
	local cpu=$1

	if ! online_cpu $cpu; then
		echo $FUNCNAME $cpu: unexpected fail >&2
		retval=1
	elif ! cpu_is_online $cpu; then
		echo $FUNCNAME $cpu: unexpected offline >&2
		retval=1
	fi
}

online_cpu_expect_fail()
{
	local cpu=$1

	if online_cpu $cpu 2> /dev/null; then
		echo $FUNCNAME $cpu: unexpected success >&2
		retval=1
	elif ! cpu_is_offline $cpu; then
		echo $FUNCNAME $cpu: unexpected online >&2
		retval=1
	fi
}

offline_cpu_expect_success()
{
	local cpu=$1

	if ! offline_cpu $cpu; then
		echo $FUNCNAME $cpu: unexpected fail >&2
		retval=1
	elif ! cpu_is_offline $cpu; then
		echo $FUNCNAME $cpu: unexpected offline >&2
		retval=1
	fi
}

offline_cpu_expect_fail()
{
	local cpu=$1

	if offline_cpu $cpu 2> /dev/null; then
		echo $FUNCNAME $cpu: unexpected success >&2
		retval=1
	elif ! cpu_is_online $cpu; then
		echo $FUNCNAME $cpu: unexpected offline >&2
		retval=1
	fi
}

online_all_hot_pluggable_cpus()
{
	for cpu in `hotpluggable_offline_cpus`; do
		online_cpu_expect_success $cpu
	done
}

offline_all_hot_pluggable_cpus()
{
	local reserve_cpu=$online_max
	for cpu in `hotpluggable_online_cpus`; do
		# Reserve one cpu oneline at least.
		if [ $cpu -eq $reserve_cpu ];then
			continue
		fi
		offline_cpu_expect_success $cpu
	done
}

allcpus=0
online_cpus=0
online_max=0
offline_cpus=0
offline_max=0
present_cpus=0
present_max=0

while getopts ah opt; do
	case $opt in
	a)
		allcpus=1
		;;
	h)
		echo "Usage $0 [ -a ]"
		echo -e "\t default offline one cpu"
		echo -e "\t run with -a option to offline all cpus"
		exit
		;;
	esac
done

prerequisite

#
# Safe test (default) - offline and online one cpu
#
if [ $allcpus -eq 0 ]; then
	echo "Limited scope test: one hotplug cpu"
	echo -e "\t (leaves cpu in the original state):"
	echo -e "\t online to offline to online: cpu $online_max"
	offline_cpu_expect_success $online_max
	online_cpu_expect_success $online_max

	if [[ $offline_cpus -gt 0 ]]; then
		echo -e "\t online to offline to online: cpu $present_max"
		online_cpu_expect_success $present_max
		offline_cpu_expect_success $present_max
		online_cpu $present_max
	fi
	exit $retval
else
	echo "Full scope test: all hotplug cpus"
	echo -e "\t online all offline cpus"
	echo -e "\t offline all online cpus"
	echo -e "\t online all offline cpus"
fi

online_all_hot_pluggable_cpus

offline_all_hot_pluggable_cpus

online_all_hot_pluggable_cpus

exit $retval
