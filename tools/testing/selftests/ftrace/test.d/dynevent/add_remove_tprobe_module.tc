#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: Generic dynamic event - add/remove tracepoint probe events on module
# requires: dynamic_events "t[:[<group>/][<event>]] <tracepoint> [<args>]":README

rmmod trace-events-sample ||:
if ! modprobe trace-events-sample ; then
  echo "No trace-events sample module - please make CONFIG_SAMPLE_TRACE_EVENTS=m"
  exit_unresolved;
fi
trap "rmmod trace-events-sample" EXIT

echo 0 > events/enable
echo > dynamic_events

TRACEPOINT1=foo_bar
TRACEPOINT2=foo_bar_with_cond

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

:;: "Try to put a probe on a tracepoint in non-loaded module" ;:
rmmod trace-events-sample

echo "t:myevent1 $TRACEPOINT1" >> dynamic_events
echo "t:myevent2 $TRACEPOINT2" >> dynamic_events

grep -q myevent1 dynamic_events
grep -q myevent2 dynamic_events
test -d events/tracepoints/myevent1
test -d events/tracepoints/myevent2

echo 1 > events/tracepoints/enable

modprobe trace-events-sample

sleep 2

grep -q "myevent1" trace
grep -q "myevent2" trace

rmmod trace-events-sample
trap "" EXIT

echo 0 > events/tracepoints/enable
echo > dynamic_events
clear_trace
