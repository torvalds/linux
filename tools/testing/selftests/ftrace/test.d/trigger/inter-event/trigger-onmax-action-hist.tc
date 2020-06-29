#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: event trigger - test inter-event histogram trigger onmax action
# requires: set_event synthetic_events events/sched/sched_process_fork/hist

fail() { #msg
    echo $1
    exit_fail
}

echo "Test create synthetic event"

echo 'wakeup_latency  u64 lat pid_t pid char comm[16]' > synthetic_events
if [ ! -d events/synthetic/wakeup_latency ]; then
    fail "Failed to create wakeup_latency synthetic event"
fi

echo "Test onmax action"

echo 'hist:keys=pid:ts0=common_timestamp.usecs if comm=="ping"' >> events/sched/sched_waking/trigger
echo 'hist:keys=next_pid:wakeup_lat=common_timestamp.usecs-$ts0:onmax($wakeup_lat).save(next_comm,prev_pid,prev_prio,prev_comm) if next_comm=="ping"' >> events/sched/sched_switch/trigger

ping $LOCALHOST -c 3
if ! grep -q "max:" events/sched/sched_switch/hist; then
    fail "Failed to create onmax action inter-event histogram"
fi

exit 0
