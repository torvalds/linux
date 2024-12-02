#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: event trigger - test snapshot-trigger
# requires: set_event events/sched/sched_process_fork/trigger snapshot

fail() { #msg
    echo $1
    exit_fail
}

FEATURE=`grep snapshot events/sched/sched_process_fork/trigger`
if [ -z "$FEATURE" ]; then
    echo "snapshot trigger is not supported"
    exit_unsupported
fi

echo "Test snapshot trigger"
echo 0 > snapshot
echo 1 > events/sched/sched_process_fork/enable
( echo "forked")
echo 'snapshot:1' > events/sched/sched_process_fork/trigger
( echo "forked")
grep sched_process_fork snapshot > /dev/null || \
    fail "snapshot trigger on sched_process_fork did not work"

reset_trigger
echo 0 > snapshot
echo 0 > events/sched/sched_process_fork/enable

echo "Test snapshot semantic errors"

! echo "snapshot+1" > events/sched/sched_process_fork/trigger
echo "snapshot" > events/sched/sched_process_fork/trigger
! echo "snapshot" > events/sched/sched_process_fork/trigger

exit 0
