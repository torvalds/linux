#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2011 Bryan Schumaker <bjschuma@netapp.com>
#
# Script for easier NFSD fault injection

# Check that debugfs has been mounted
DEBUGFS=`cat /proc/mounts | grep debugfs`
if [ "$DEBUGFS" == "" ]; then
	echo "debugfs does not appear to be mounted!"
	echo "Please mount debugfs and try again"
	exit 1
fi

# Check that the fault injection directory exists
DEBUGDIR=`echo $DEBUGFS | awk '{print $2}'`/nfsd
if [ ! -d "$DEBUGDIR" ]; then
	echo "$DEBUGDIR does not exist"
	echo "Check that your .config selects CONFIG_NFSD_FAULT_INJECTION"
	exit 1
fi

function help()
{
	echo "Usage $0 injection_type [count]"
	echo ""
	echo "Injection types are:"
	ls $DEBUGDIR
	exit 1
}

if [ $# == 0 ]; then
	help
elif [ ! -f $DEBUGDIR/$1 ]; then
	help
elif [ $# != 2 ]; then
	COUNT=0
else
	COUNT=$2
fi

BEFORE=`mktemp`
AFTER=`mktemp`
dmesg > $BEFORE
echo $COUNT > $DEBUGDIR/$1
dmesg > $AFTER
# Capture lines that only exist in the $AFTER file
diff $BEFORE $AFTER | grep ">"
rm -f $BEFORE $AFTER
