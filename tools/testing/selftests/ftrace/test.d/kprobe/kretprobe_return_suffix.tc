#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: Kretprobe %%return suffix test
# requires: kprobe_events '<symbol>[+<offset>]%return':README

# Test for kretprobe by "r"
echo 'r:myprobeaccept vfs_read' > kprobe_events
RESULT1=`cat kprobe_events`

# Test for kretprobe by "%return"
echo 'p:myprobeaccept vfs_read%return' > kprobe_events
RESULT2=`cat kprobe_events`

if [ "$RESULT1" != "$RESULT2" ]; then
	echo "Error: %return suffix didn't make a return probe."
	echo "r-command: $RESULT1"
	echo "%return: $RESULT2"
	exit_fail
fi

echo > kprobe_events
