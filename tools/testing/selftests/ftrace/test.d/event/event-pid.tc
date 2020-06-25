#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: event tracing - restricts events based on pid
# requires: set_event set_event_pid events/sched
# flags: instance

do_reset() {
    echo > set_event
    echo > set_event_pid
    echo 0 > options/event-fork
    clear_trace
}

fail() { #msg
    do_reset
    echo $1
    exit_fail
}

echo 0 > options/event-fork

echo 1 > events/sched/sched_switch/enable

yield

count=`cat trace | grep sched_switch | wc -l`
if [ $count -eq 0 ]; then
    fail "sched_switch events are not recorded"
fi

do_reset

read mypid rest < /proc/self/stat

echo $mypid > set_event_pid
grep -q $mypid set_event_pid
echo 'sched:sched_switch' > set_event

yield

count=`cat trace | grep sched_switch | grep -v "pid=$mypid" | wc -l`
if [ $count -ne 0 ]; then
    fail "sched_switch events from other task are recorded"
fi

do_reset

echo $mypid > set_event_pid
echo 1 > options/event-fork
echo 1 > events/sched/sched_switch/enable

yield

count=`cat trace | grep sched_switch | grep -v "pid=$mypid" | wc -l`
if [ $count -eq 0 ]; then
    fail "sched_switch events from other task are not recorded"
fi

do_reset

exit 0
