#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: event trigger - test inter-event histogram trigger snapshot action
# requires: set_event snapshot events/sched/sched_process_fork/hist "onchange(var)":README "snapshot()":README

fail() { #msg
    echo $1
    exit_fail
}

echo "Test snapshot action"

echo 1 > events/sched/enable

echo 'hist:keys=comm:newprio=prio:onchange($newprio).save(comm,prio):onchange($newprio).snapshot() if comm=="ping"' >> events/sched/sched_waking/trigger

ping $LOCALHOST -c 3
nice -n 1 ping $LOCALHOST -c 3

echo 0 > tracing_on

if ! grep -q "changed:" events/sched/sched_waking/hist; then
    fail "Failed to create onchange action inter-event histogram"
fi

if ! grep -q "comm=ping" snapshot; then
    fail "Failed to create snapshot action inter-event histogram"
fi

exit 0
