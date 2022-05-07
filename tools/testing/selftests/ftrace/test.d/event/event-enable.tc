#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: event tracing - enable/disable with event level files
# requires: set_event events/sched
# flags: instance

do_reset() {
    echo > set_event
    clear_trace
}

fail() { #msg
    echo $1
    exit_fail
}

echo 'sched:sched_switch' > set_event

yield

count=`cat trace | grep sched_switch | wc -l`
if [ $count -eq 0 ]; then
    fail "sched_switch events are not recorded"
fi

do_reset

echo 1 > events/sched/sched_switch/enable

yield

count=`cat trace | grep sched_switch | wc -l`
if [ $count -eq 0 ]; then
    fail "sched_switch events are not recorded"
fi

do_reset

echo 0 > events/sched/sched_switch/enable

yield

count=`cat trace | grep sched_switch | wc -l`
if [ $count -ne 0 ]; then
    fail "sched_switch events should not be recorded"
fi

exit 0
