#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: Generic dynamic event - add/remove tracepoint probe events
# requires: dynamic_events "t[:[<group>/][<event>]] <tracepoint> [<args>]":README

echo 0 > events/enable
echo > dynamic_events

TRACEPOINT1=kmem_cache_alloc
TRACEPOINT2=kmem_cache_free

echo "t:myevent1 $TRACEPOINT1" >> dynamic_events
echo "t:myevent2 $TRACEPOINT2" >> dynamic_events

grep -q myevent1 dynamic_events
grep -q myevent2 dynamic_events
test -d events/tracepoints/myevent1
test -d events/tracepoints/myevent2

echo "-:myevent2" >> dynamic_events

grep -q myevent1 dynamic_events
! grep -q myevent2 dynamic_events

echo > dynamic_events

clear_trace
