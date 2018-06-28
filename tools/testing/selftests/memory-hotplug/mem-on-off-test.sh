#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

SYSFS=

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

prerequisite()
{
	msg="skip all tests:"

	if [ $UID != 0 ]; then
		echo $msg must be run as root >&2
		exit $ksft_skip
	fi

	SYSFS=`mount -t sysfs | head -1 | awk '{ print $3 }'`

	if [ ! -d "$SYSFS" ]; then
		echo $msg sysfs is not mounted >&2
		exit $ksft_skip
	fi

	if ! ls $SYSFS/devices/system/memory/memory* > /dev/null 2>&1; then
		echo $msg memory hotplug is not supported >&2
		exit $ksft_skip
	fi

	if ! grep -q 1 $SYSFS/devices/system/memory/memory*/removable; then
		echo $msg no hot-pluggable memory >&2
		exit $ksft_skip
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

hotpluggable_offline_memory()
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
		return 1
	elif ! memory_is_online $memory; then
		echo $FUNCNAME $memory: unexpected offline >&2
		return 1
	fi
	return 0
}

online_memory_expect_fail()
{
	local memory=$1

	if online_memory $memory 2> /dev/null; then
		echo $FUNCNAME $memory: unexpected success >&2
		return 1
	elif ! memory_is_offline $memory; then
		echo $FUNCNAME $memory: unexpected online >&2
		return 1
	fi
	return 0
}

offline_memory_expect_success()
{
	local memory=$1

	if ! offline_memory $memory; then
		echo $FUNCNAME $memory: unexpected fail >&2
		return 1
	elif ! memory_is_offline $memory; then
		echo $FUNCNAME $memory: unexpected offline >&2
		return 1
	fi
	return 0
}

offline_memory_expect_fail()
{
	local memory=$1

	if offline_memory $memory 2> /dev/null; then
		echo $FUNCNAME $memory: unexpected success >&2
		return 1
	elif ! memory_is_online $memory; then
		echo $FUNCNAME $memory: unexpected offline >&2
		return 1
	fi
	return 0
}

error=-12
priority=0
# Run with default of ratio=2 for Kselftest run
ratio=2
retval=0

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
		if [ "$ratio" -gt 100 ] || [ "$ratio" -lt 0 ]; then
			echo "The percentage should be an integer within 0~100 range"
			exit 1
		fi
		;;
	esac
done

if ! [ "$error" -ge -4095 -a "$error" -lt 0 ]; then
	echo "error code must be -4095 <= errno < 0" >&2
	exit 1
fi

prerequisite

echo "Test scope: $ratio% hotplug memory"

#
# Online all hot-pluggable memory
#
hotpluggable_num=`hotpluggable_offline_memory | wc -l`
echo -e "\t online all hot-pluggable memory in offline state:"
if [ "$hotpluggable_num" -gt 0 ]; then
	for memory in `hotpluggable_offline_memory`; do
		echo "offline->online memory$memory"
		if ! online_memory_expect_success $memory; then
			retval=1
		fi
	done
else
	echo -e "\t\t SKIPPED - no hot-pluggable memory in offline state"
fi

#
# Offline $ratio percent of hot-pluggable memory
#
hotpluggable_num=`hotpluggable_online_memory | wc -l`
target=`echo "a=$hotpluggable_num*$ratio; if ( a%100 ) a/100+1 else a/100" | bc`
echo -e "\t offline $ratio% hot-pluggable memory in online state"
echo -e "\t trying to offline $target out of $hotpluggable_num memory block(s):"
for memory in `hotpluggable_online_memory`; do
	if [ "$target" -gt 0 ]; then
		echo "online->offline memory$memory"
		if offline_memory_expect_success $memory; then
			target=$(($target - 1))
		fi
	fi
done
if [ "$target" -gt 0 ]; then
	retval=1
	echo -e "\t\t FAILED - unable to offline some memory blocks, device busy?"
fi

#
# Online all hot-pluggable memory again
#
hotpluggable_num=`hotpluggable_offline_memory | wc -l`
echo -e "\t online all hot-pluggable memory in offline state:"
if [ "$hotpluggable_num" -gt 0 ]; then
	for memory in `hotpluggable_offline_memory`; do
		echo "offline->online memory$memory"
		if ! online_memory_expect_success $memory; then
			retval=1
		fi
	done
else
	echo -e "\t\t SKIPPED - no hot-pluggable memory in offline state"
fi

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
		exit $retval
	fi

	if [ ! -d $NOTIFIER_ERR_INJECT_DIR ]; then
		echo $msg memory-notifier-error-inject module is not available >&2
		exit $retval
	fi
}

echo -e "\t Test with memory notifier error injection"
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
for memory in `hotpluggable_offline_memory`; do
	online_memory_expect_fail $memory
done

#
# Online all hot-pluggable memory
#
echo 0 > $NOTIFIER_ERR_INJECT_DIR/actions/MEM_GOING_ONLINE/error
for memory in `hotpluggable_offline_memory`; do
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

exit $retval
