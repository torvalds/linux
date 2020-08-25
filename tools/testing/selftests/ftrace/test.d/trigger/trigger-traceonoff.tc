#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: event trigger - test traceon/off trigger
# requires: set_event events/sched/sched_process_fork/trigger

fail() { #msg
    echo $1
    exit_fail
}

echo "Test traceoff trigger"
echo 1 > tracing_on
echo 'traceoff' > events/sched/sched_process_fork/trigger
( echo "forked")
if [ `cat tracing_on` -ne 0 ]; then
    fail "traceoff trigger on sched_process_fork did not work"
fi

reset_trigger

echo "Test traceon trigger"
echo 0 > tracing_on
echo 'traceon' > events/sched/sched_process_fork/trigger
( echo "forked")
if [ `cat tracing_on` -ne 1 ]; then
    fail "traceoff trigger on sched_process_fork did not work"
fi

reset_trigger

echo "Test semantic error for traceoff/on trigger"
! echo 'traceoff:badparam' > events/sched/sched_process_fork/trigger
! echo 'traceoff+0' > events/sched/sched_process_fork/trigger
echo 'traceon' > events/sched/sched_process_fork/trigger
! echo 'traceon' > events/sched/sched_process_fork/trigger
! echo 'traceoff' > events/sched/sched_process_fork/trigger

exit 0
