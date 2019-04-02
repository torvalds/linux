#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2011 Bryan Schumaker <bjschuma@netapp.com>
#
# Script for easier NFSD fault injection

# Check that defs has been mounted
DEFS=`cat /proc/mounts | grep defs`
if [ "$DEFS" == "" ]; then
	echo "defs does not appear to be mounted!"
	echo "Please mount defs and try again"
	exit 1
fi

# Check that the fault injection directory exists
DEDIR=`echo $DEFS | awk '{print $2}'`/nfsd
if [ ! -d "$DEDIR" ]; then
	echo "$DEDIR does not exist"
	echo "Check that your .config selects CONFIG_NFSD_FAULT_INJECTION"
	exit 1
fi

function help()
{
	echo "Usage $0 injection_type [count]"
	echo ""
	echo "Injection types are:"
	ls $DEDIR
	exit 1
}

if [ $# == 0 ]; then
	help
elif [ ! -f $DEDIR/$1 ]; then
	help
elif [ $# != 2 ]; then
	COUNT=0
else
	COUNT=$2
fi

BEFORE=`mktemp`
AFTER=`mktemp`
dmesg > $BEFORE
echo $COUNT > $DEDIR/$1
dmesg > $AFTER
# Capture lines that only exist in the $AFTER file
diff $BEFORE $AFTER | grep ">"
rm -f $BEFORE $AFTER
