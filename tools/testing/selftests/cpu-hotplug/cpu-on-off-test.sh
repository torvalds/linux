#!/bin/bash

SYSFS=

prerequisite()
{
	msg="skip all tests:"

	if [ $UID != 0 ]; then
		echo $msg must be run as root >&2
		exit 0
	fi

	taskset -p 01 $$

	SYSFS=`mount -t sysfs | head -1 | awk '{ print $3 }'`

	if [ ! -d "$SYSFS" ]; then
		echo $msg sysfs is not mounted >&2
		exit 0
	fi

	if ! ls $SYSFS/devices/system/cpu/cpu* > /dev/null 2>&1; then
		echo $msg cpu hotplug is not supported >&2
		exit 0
	fi

	echo "CPU online/offline summary:"
	online_cpus=`cat $SYSFS/devices/system/cpu/online`
	online_max=${online_cpus##*-}
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

hotplaggable_offline_cpus()
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
	elif ! cpu_is_online $cpu; then
		echo $FUNCNAME $cpu: unexpected offline >&2
	fi
}

online_cpu_expect_fail()
{
	local cpu=$1

	if online_cpu $cpu 2> /dev/null; then
		echo $FUNCNAME $cpu: unexpected success >&2
	elif ! cpu_is_offline $cpu; then
		echo $FUNCNAME $cpu: unexpected online >&2
	fi
}

offline_cpu_expect_success()
{
	local cpu=$1

	if ! offline_cpu $cpu; then
		echo $FUNCNAME $cpu: unexpected fail >&2
	elif ! cpu_is_offline $cpu; then
		echo $FUNCNAME $cpu: unexpected offline >&2
	fi
}

offline_cpu_expect_fail()
{
	local cpu=$1

	if offline_cpu $cpu 2> /dev/null; then
		echo $FUNCNAME $cpu: unexpected success >&2
	elif ! cpu_is_online $cpu; then
		echo $FUNCNAME $cpu: unexpected offline >&2
	fi
}

error=-12
allcpus=0
priority=0
online_cpus=0
online_max=0
offline_cpus=0
offline_max=0

while getopts e:ahp: opt; do
	case $opt in
	e)
		error=$OPTARG
		;;
	a)
		allcpus=1
		;;
	h)
		echo "Usage $0 [ -a ] [ -e errno ] [ -p notifier-priority ]"
		echo -e "\t default offline one cpu"
		echo -e "\t run with -a option to offline all cpus"
		exit
		;;
	p)
		priority=$OPTARG
		;;
	esac
done

if ! [ "$error" -ge -4095 -a "$error" -lt 0 ]; then
	echo "error code must be -4095 <= errno < 0" >&2
	exit 1
fi

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
		echo -e "\t offline to online to offline: cpu $offline_max"
		online_cpu_expect_success $offline_max
		offline_cpu_expect_success $offline_max
	fi
	exit 0
else
	echo "Full scope test: all hotplug cpus"
	echo -e "\t online all offline cpus"
	echo -e "\t offline all online cpus"
	echo -e "\t online all offline cpus"
fi

#
# Online all hot-pluggable CPUs
#
for cpu in `hotplaggable_offline_cpus`; do
	online_cpu_expect_success $cpu
done

#
# Offline all hot-pluggable CPUs
#
for cpu in `hotpluggable_online_cpus`; do
	offline_cpu_expect_success $cpu
done

#
# Online all hot-pluggable CPUs again
#
for cpu in `hotplaggable_offline_cpus`; do
	online_cpu_expect_success $cpu
done

#
# Test with cpu notifier error injection
#

DEBUGFS=`mount -t debugfs | head -1 | awk '{ print $3 }'`
NOTIFIER_ERR_INJECT_DIR=$DEBUGFS/notifier-error-inject/cpu

prerequisite_extra()
{
	msg="skip extra tests:"

	/sbin/modprobe -q -r cpu-notifier-error-inject
	/sbin/modprobe -q cpu-notifier-error-inject priority=$priority

	if [ ! -d "$DEBUGFS" ]; then
		echo $msg debugfs is not mounted >&2
		exit 0
	fi

	if [ ! -d $NOTIFIER_ERR_INJECT_DIR ]; then
		echo $msg cpu-notifier-error-inject module is not available >&2
		exit 0
	fi
}

prerequisite_extra

#
# Offline all hot-pluggable CPUs
#
echo 0 > $NOTIFIER_ERR_INJECT_DIR/actions/CPU_DOWN_PREPARE/error
for cpu in `hotpluggable_online_cpus`; do
	offline_cpu_expect_success $cpu
done

#
# Test CPU hot-add error handling (offline => online)
#
echo $error > $NOTIFIER_ERR_INJECT_DIR/actions/CPU_UP_PREPARE/error
for cpu in `hotplaggable_offline_cpus`; do
	online_cpu_expect_fail $cpu
done

#
# Online all hot-pluggable CPUs
#
echo 0 > $NOTIFIER_ERR_INJECT_DIR/actions/CPU_UP_PREPARE/error
for cpu in `hotplaggable_offline_cpus`; do
	online_cpu_expect_success $cpu
done

#
# Test CPU hot-remove error handling (online => offline)
#
echo $error > $NOTIFIER_ERR_INJECT_DIR/actions/CPU_DOWN_PREPARE/error
for cpu in `hotpluggable_online_cpus`; do
	offline_cpu_expect_fail $cpu
done

echo 0 > $NOTIFIER_ERR_INJECT_DIR/actions/CPU_DOWN_PREPARE/error
/sbin/modprobe -q -r cpu-notifier-error-inject
