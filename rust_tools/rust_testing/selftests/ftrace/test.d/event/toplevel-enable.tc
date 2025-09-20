#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: event tracing - enable/disable with top level files
# requires: available_events set_event events/enable

do_reset() {
    echo > set_event
    clear_trace
}

fail() { #msg
    echo $1
    exit_fail
}

echo '*:*' > set_event

yield

echo 0 > tracing_on

count=`head -n 128 trace | grep -v ^# | wc -l`
if [ $count -eq 0 ]; then
    fail "none of events are recorded"
fi

do_reset

echo 1 > events/enable
echo 1 > tracing_on

yield

echo 0 > tracing_on
count=`head -n 128 trace | grep -v ^# | wc -l`
if [ $count -eq 0 ]; then
    fail "none of events are recorded"
fi

do_reset

echo 0 > events/enable

yield

count=`cat trace | grep -v ^# | wc -l`
if [ $count -ne 0 ]; then
    fail "any of events should not be recorded"
fi

exit 0
