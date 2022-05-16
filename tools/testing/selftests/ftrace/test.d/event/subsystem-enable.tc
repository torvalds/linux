#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: event tracing - enable/disable with subsystem level files
# requires: set_event events/sched/enable
# flags: instance

do_reset() {
    echo > set_event
    clear_trace
}

fail() { #msg
    echo $1
    exit_fail
}

echo 'sched:*' > set_event

yield

count=`cat trace | grep -v ^# | awk '{ print $5 }' | sort -u | wc -l`
if [ $count -lt 3 ]; then
    fail "at least fork, exec and exit events should be recorded"
fi

do_reset

echo 1 > events/sched/enable

yield

count=`cat trace | grep -v ^# | awk '{ print $5 }' | sort -u | wc -l`
if [ $count -lt 3 ]; then
    fail "at least fork, exec and exit events should be recorded"
fi

do_reset

echo 0 > events/sched/enable

yield

count=`cat trace | grep -v ^# | awk '{ print $5 }' | sort -u | wc -l`
if [ $count -ne 0 ]; then
    fail "any of scheduler events should not be recorded"
fi

exit 0
