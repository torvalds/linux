#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: event trigger - test inter-event histogram trigger onchange action
# requires: set_event "onchange(var)":README

fail() { #msg
    echo $1
    exit_fail
}

echo "Test onchange action"

echo 'hist:keys=comm:newprio=prio:onchange($newprio).save(comm,prio) if comm=="ping"' >> events/sched/sched_waking/trigger

ping $LOCALHOST -c 3
nice -n 1 ping $LOCALHOST -c 3

if ! grep -q "changed:" events/sched/sched_waking/hist; then
    fail "Failed to create onchange action inter-event histogram"
fi

exit 0
