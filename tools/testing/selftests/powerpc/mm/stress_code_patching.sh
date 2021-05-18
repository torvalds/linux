#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later

TIMEOUT=30

DEBUFS_DIR=`cat /proc/mounts | grep debugfs | awk '{print $2}'`
if [ ! -e "$DEBUFS_DIR" ]
then
	echo "debugfs not found, skipping" 1>&2
	exit 4
fi

if [ ! -e "$DEBUFS_DIR/tracing/current_tracer" ]
then
	echo "Tracing files not found, skipping" 1>&2
	exit 4
fi


echo "Testing for spurious faults when mapping kernel memory..."

if grep -q "FUNCTION TRACING IS CORRUPTED" "$DEBUFS_DIR/tracing/trace"
then
	echo "FAILED: Ftrace already dead. Probably due to a spurious fault" 1>&2
	exit 1
fi

dmesg -C
START_TIME=`date +%s`
END_TIME=`expr $START_TIME + $TIMEOUT`
while [ `date +%s` -lt $END_TIME ]
do
	echo function > $DEBUFS_DIR/tracing/current_tracer
	echo nop > $DEBUFS_DIR/tracing/current_tracer
	if dmesg | grep -q 'ftrace bug'
	then
		break
	fi
done

echo nop > $DEBUFS_DIR/tracing/current_tracer
if dmesg | grep -q 'ftrace bug'
then
	echo "FAILED: Mapping kernel memory causes spurious faults" 1>&2
	exit 1
else
	echo "OK: Mapping kernel memory does not cause spurious faults"
	exit 0
fi
