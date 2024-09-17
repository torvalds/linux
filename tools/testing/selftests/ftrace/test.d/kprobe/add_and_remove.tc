#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: Kprobe dynamic event - adding and removing
# requires: kprobe_events

echo p:myevent $FUNCTION_FORK > kprobe_events
grep myevent kprobe_events
test -d events/kprobes/myevent
echo > kprobe_events
