#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: event trigger - test field variable support
# requires: set_event synthetic_events events/sched/sched_process_fork/hist ping:program

fail() { #msg
    echo $1
    exit_fail
}

echo "Test field variable support"

echo 'wakeup_latency u64 lat; pid_t pid; int prio; char comm[16]' > synthetic_events
echo 'hist:keys=comm:ts0=common_timestamp.usecs if comm=="ping"' > events/sched/sched_waking/trigger
echo 'hist:keys=next_comm:wakeup_lat=common_timestamp.usecs-$ts0:onmatch(sched.sched_waking).wakeup_latency($wakeup_lat,next_pid,sched.sched_waking.prio,next_comm) if next_comm=="ping"' > events/sched/sched_switch/trigger
echo 'hist:keys=pid,prio,comm:vals=lat:sort=pid,prio' > events/synthetic/wakeup_latency/trigger

ping $LOCALHOST -c 3
if ! grep -q "ping" events/synthetic/wakeup_latency/hist; then
    fail "Failed to create inter-event histogram"
fi

if ! grep -q "synthetic_prio=prio" events/sched/sched_waking/hist; then
    fail "Failed to create histogram with field variable"
fi

echo '!hist:keys=next_comm:wakeup_lat=common_timestamp.usecs-$ts0:onmatch(sched.sched_waking).wakeup_latency($wakeup_lat,next_pid,sched.sched_waking.prio,next_comm) if next_comm=="ping"' >> events/sched/sched_switch/trigger

if grep -q "synthetic_prio=prio" events/sched/sched_waking/hist; then
    fail "Failed to remove histogram with field variable"
fi

exit 0
