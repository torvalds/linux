#!/bin/bash

SYSFS=

prerequisite()
{
	msg="skip all tests:"

	if [ $UID != 0 ]; then
		echo $msg must be run as root >&2
		exit 0
	fi

	SYSFS=`mount -t sysfs | head -1 | awk '{ print $3 }'`

	if [ ! -d "$SYSFS" ]; then
		echo $msg sysfs is not mounted >&2
		exit 0
	fi

	if ! ls $SYSFS/devices/system/memory/memory* > /dev/null 2>&1; then
		echo $msg memory hotplug is not supported >&2
		exit 0
	fi
}

#
# list all hot-pluggable memory
#
hotpluggable_memory()
{
	local state=${1:-.\*}

	for memory in $SYSFS/devices/system/memory/memory*; do
		if grep -q 1 $memory/removable &&
		   grep -q $state $memory/state; then
			echo ${memory##/*/memory}
		fi
	done
}

hotplaggable_offline_memory()
{
	hotpluggable_memory offline
}

hotpluggable_online_memory()
{
	hotpluggable_memory online
}

memory_is_online()
{
	grep -q online $SYSFS/devices/system/memory/memory$1/state
}

memory_is_offline()
{
	grep -q offline $SYSFS/devices/system/memory/memory$1/state
}

online_memory()
{
	echo online > $SYSFS/devices/system/memory/memory$1/state
}

offline_memory()
{
	echo offline > $SYSFS/devices/system/memory/memory$1/state
}

online_memory_expect_success()
{
	local memory=$1

	if ! online_memory $memory; then
		echo $FUNCNAME $memory: unexpected fail >&2
	elif ! memory_is_online $memory; then
		echo $FUNCNAME $memory: unexpected offline >&2
	fi
}

online_memory_expect_fail()
{
	local memory=$1

	if online_memory $memory 2> /dev/null; then
		echo $FUNCNAME $memory: unexpected success >&2
	elif ! memory_is_offline $memory; then
		echo $FUNCNAME $memory: unexpected online >&2
	fi
}

offline_memory_expect_success()
{
	local memory=$1

	if ! offline_memory $memory; then
		echo $FUNCNAME $memory: unexpected fail >&2
	elif ! memory_is_offline $memory; then
		echo $FUNCNAME $memory: unexpected offline >&2
	fi
}

offline_memory_expect_fail()
{
	local memory=$1

	if offline_memory $memory 2> /dev/null; then
		echo $FUNCNAME $memory: unexpected success >&2
	elif ! memory_is_online $memory; then
		echo $FUNCNAME $memory: unexpected offline >&2
	fi
}

error=-12
priority=0
ratio=10

while getopts e:hp:r: opt; do
	case $opt in
	e)
		error=$OPTARG
		;;
	h)
		echo "Usage $0 [ -e errno ] [ -p notifier-priority ] [ -r percent-of-memory-to-offline ]"
		exit
		;;
	p)
		priority=$OPTARG
		;;
	r)
		ratio=$OPTARG
		;;
	esac
done

if ! [ "$error" -ge -4095 -a "$error" -lt 0 ]; then
	echo "error code must be -4095 <= errno < 0" >&2
	exit 1
fi

prerequisite

echo "Test scope: $ratio% hotplug memory"
echo -e "\t online all hotplug memory in offline state"
echo -e "\t offline $ratio% hotplug memory in online state"
echo -e "\t online all hotplug memory in offline state"

#
# Online all hot-pluggable memory
#
for memory in `hotplaggable_offline_memory`; do
	echo offline-online $memory
	online_memory_expect_success $memory
done

#
# Offline $ratio percent of hot-pluggable memory
#
for memory in `hotpluggable_online_memory`; do
	if [ $((RANDOM % 100)) -lt $ratio ]; then
		echo online-offline $memory
		offline_memory_expect_success $memory
	fi
done

#
# Online all hot-pluggable memory again
#
for memory in `hotplaggable_offline_memory`; do
	echo offline-online $memory
	online_memory_expect_success $memory
done

#
# Test with memory notifier error injection
#

DEBUGFS=`mount -t debugfs | head -1 | awk '{ print $3 }'`
NOTIFIER_ERR_INJECT_DIR=$DEBUGFS/notifier-error-inject/memory

prerequisite_extra()
{
	msg="skip extra tests:"

	/sbin/modprobe -q -r memory-notifier-error-inject
	/sbin/modprobe -q memory-notifier-error-inject priority=$priority

	if [ ! -d "$DEBUGFS" ]; then
		echo $msg debugfs is not mounted >&2
		exit 0
	fi

	if [ ! -d $NOTIFIER_ERR_INJECT_DIR ]; then
		echo $msg memory-notifier-error-inject module is not available >&2
		exit 0
	fi
}

prerequisite_extra

#
# Offline $ratio percent of hot-pluggable memory
#
echo 0 > $NOTIFIER_ERR_INJECT_DIR/actions/MEM_GOING_OFFLINE/error
for memory in `hotpluggable_online_memory`; do
	if [ $((RANDOM % 100)) -lt $ratio ]; then
		offline_memory_expect_success $memory
	fi
done

#
# Test memory hot-add error handling (offline => online)
#
echo $error > $NOTIFIER_ERR_INJECT_DIR/actions/MEM_GOING_ONLINE/error
for memory in `hotplaggable_offline_memory`; do
	online_memory_expect_fail $memory
done

#
# Online all hot-pluggable memory
#
echo 0 > $NOTIFIER_ERR_INJECT_DIR/actions/MEM_GOING_ONLINE/error
for memory in `hotplaggable_offline_memory`; do
	online_memory_expect_success $memory
done

#
# Test memory hot-remove error handling (online => offline)
#
echo $error > $NOTIFIER_ERR_INJECT_DIR/actions/MEM_GOING_OFFLINE/error
for memory in `hotpluggable_online_memory`; do
	offline_memory_expect_fail $memory
done

echo 0 > $NOTIFIER_ERR_INJECT_DIR/actions/MEM_GOING_OFFLINE/error
/sbin/modprobe -q -r memory-notifier-error-inject
