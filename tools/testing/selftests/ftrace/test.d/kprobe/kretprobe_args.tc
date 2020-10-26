#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: Kretprobe dynamic event with arguments
# requires: kprobe_events

# Add new kretprobe event
echo "r:testprobe2 $FUNCTION_FORK \$retval" > kprobe_events
grep testprobe2 kprobe_events | grep -q 'arg1=\$retval'
test -d events/kprobes/testprobe2

echo 1 > events/kprobes/testprobe2/enable
( echo "forked")

cat trace | grep testprobe2 | grep -q "<- $FUNCTION_FORK"

echo 0 > events/kprobes/testprobe2/enable
echo '-:testprobe2' >> kprobe_events
clear_trace
test -d events/kprobes/testprobe2 && exit_fail || exit_pass
