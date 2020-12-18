#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: Create/delete multiprobe on kprobe event
# requires: kprobe_events "Create/append/":README

# Choose 2 symbols for target
SYM1=$FUNCTION_FORK
SYM2=do_exit
EVENT_NAME=kprobes/testevent

DEF1="p:$EVENT_NAME $SYM1"
DEF2="p:$EVENT_NAME $SYM2"

:;: "Define an event which has 2 probes" ;:
echo $DEF1 >> kprobe_events
echo $DEF2 >> kprobe_events
cat kprobe_events | grep "$DEF1"
cat kprobe_events | grep "$DEF2"

:;: "Remove the event by name (should remove both)" ;:
echo "-:$EVENT_NAME" >> kprobe_events
test `cat kprobe_events | wc -l` -eq 0

:;: "Remove just 1 event" ;:
echo $DEF1 >> kprobe_events
echo $DEF2 >> kprobe_events
echo "-:$EVENT_NAME $SYM1" >> kprobe_events
! cat kprobe_events | grep "$DEF1"
cat kprobe_events | grep "$DEF2"

:;: "Appending different type must fail" ;:
! echo "$DEF1 \$stack" >> kprobe_events
