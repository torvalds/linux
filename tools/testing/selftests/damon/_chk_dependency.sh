#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

DBGFS=$(grep debugfs /proc/mounts --max-count 1 | awk '{print $2}')
if [ "$DBGFS" = "" ]
then
	echo "debugfs not mounted"
	exit $ksft_skip
fi

DBGFS+="/damon"

if [ $EUID -ne 0 ];
then
	echo "Run as root"
	exit $ksft_skip
fi

if [ ! -d "$DBGFS" ]
then
	echo "$DBGFS not found"
	exit $ksft_skip
fi

if [ -f "$DBGFS/monitor_on_DEPRECATED" ]
then
	monitor_on_file="monitor_on_DEPRECATED"
else
	monitor_on_file="monitor_on"
fi

for f in attrs target_ids "$monitor_on_file"
do
	if [ ! -f "$DBGFS/$f" ]
	then
		echo "$f not found"
		exit 1
	fi
done

permission_error="Operation not permitted"
for f in attrs target_ids "$monitor_on_file"
do
	status=$( cat "$DBGFS/$f" 2>&1 )
	if [ "${status#*$permission_error}" != "$status" ]; then
		echo "Permission for reading $DBGFS/$f denied; maybe secureboot enabled?"
		exit $ksft_skip
	fi
done
