#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: event trigger - test inter-event histogram trigger trace action with dynamic string param (legacy stack)
# requires: set_event synthetic_events events/sched/sched_process_exec/hist "long[] stack' >> synthetic_events":README

fail() { #msg
    echo $1
    exit_fail
}

echo "Test create synthetic event with stack"

# Test the old stacktrace keyword (for backward compatibility)
echo 's:wake_lat pid_t pid; u64 delta; unsigned long[] stack;' > dynamic_events
echo 'hist:keys=next_pid:ts=common_timestamp.usecs,st=stacktrace  if prev_state == 1||prev_state == 2' >> events/sched/sched_switch/trigger
echo 'hist:keys=prev_pid:delta=common_timestamp.usecs-$ts,s=$st:onmax($delta).trace(wake_lat,prev_pid,$delta,$s)' >> events/sched/sched_switch/trigger
echo 1 > events/synthetic/wake_lat/enable
sleep 1

if ! grep -q "=>.*sched" trace; then
    fail "Failed to create synthetic event with stack"
fi

exit 0
