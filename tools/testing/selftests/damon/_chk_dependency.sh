#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

DBGFS=/sys/kernel/debug/damon

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

for f in attrs target_ids monitor_on
do
	if [ ! -f "$DBGFS/$f" ]
	then
		echo "$f not found"
		exit 1
	fi
done
