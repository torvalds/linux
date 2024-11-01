#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: event trigger - test multiple actions on hist trigger
# requires: set_event synthetic_events events/sched/sched_process_fork/hist

fail() { #msg
    echo $1
    exit_fail
}

echo "Test multiple actions on hist trigger"
echo 'wakeup_latency u64 lat; pid_t pid' >> synthetic_events
TRIGGER1=events/sched/sched_wakeup/trigger
TRIGGER2=events/sched/sched_switch/trigger

echo 'hist:keys=pid:ts0=common_timestamp.usecs if comm=="cyclictest"' > $TRIGGER1
echo 'hist:keys=next_pid:wakeup_lat=common_timestamp.usecs-$ts0 if next_comm=="cyclictest"' >> $TRIGGER2
echo 'hist:keys=next_pid:onmatch(sched.sched_wakeup).wakeup_latency(sched.sched_switch.$wakeup_lat,next_pid) if next_comm=="cyclictest"' >> $TRIGGER2
echo 'hist:keys=next_pid:onmatch(sched.sched_wakeup).wakeup_latency(sched.sched_switch.$wakeup_lat,prev_pid) if next_comm=="cyclictest"' >> $TRIGGER2
echo 'hist:keys=next_pid if next_comm=="cyclictest"' >> $TRIGGER2

exit 0
