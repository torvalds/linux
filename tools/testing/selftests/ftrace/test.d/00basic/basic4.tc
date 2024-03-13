#!/bin/sh
# description: Basic event tracing check
test -f available_events -a -f set_event -a -d events
# check scheduler events are available
grep -q sched available_events && exit_pass || exit_fail
