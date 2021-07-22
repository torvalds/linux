#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: Kprobe dynamic event with arguments
# requires: kprobe_events

echo "p:testprobe $FUNCTION_FORK \$stack \$stack0 +0(\$stack)" > kprobe_events
grep testprobe kprobe_events | grep -q 'arg1=\$stack arg2=\$stack0 arg3=+0(\$stack)'
test -d events/kprobes/testprobe

echo 1 > events/kprobes/testprobe/enable
( echo "forked")
grep testprobe trace | grep "$FUNCTION_FORK" | \
  grep -q 'arg1=0x[[:xdigit:]]* arg2=0x[[:xdigit:]]* arg3=0x[[:xdigit:]]*$'

echo 0 > events/kprobes/testprobe/enable
echo "-:testprobe" >> kprobe_events
clear_trace
test -d events/kprobes/testprobe && exit_fail || exit_pass

