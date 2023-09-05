#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: Generic dynamic event - add/remove fprobe events
# requires: dynamic_events "f[:[<group>/][<event>]] <func-name>[%return] [<args>]":README

echo 0 > events/enable
echo > dynamic_events

PLACE=$FUNCTION_FORK

echo "f:myevent1 $PLACE" >> dynamic_events
echo "f:myevent2 $PLACE%return" >> dynamic_events

grep -q myevent1 dynamic_events
grep -q myevent2 dynamic_events
test -d events/fprobes/myevent1
test -d events/fprobes/myevent2

echo "-:myevent2" >> dynamic_events

grep -q myevent1 dynamic_events
! grep -q myevent2 dynamic_events

echo > dynamic_events

clear_trace
