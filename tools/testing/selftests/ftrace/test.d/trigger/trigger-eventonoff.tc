#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: event trigger - test event enable/disable trigger
# requires: set_event events/sched/sched_process_fork/trigger
# flags: instance

fail() { #msg
    echo $1
    exit_fail
}

FEATURE=`grep enable_event events/sched/sched_process_fork/trigger`
if [ -z "$FEATURE" ]; then
    echo "event enable/disable trigger is not supported"
    exit_unsupported
fi

echo "Test enable_event trigger"
echo 0 > events/sched/sched_switch/enable
echo 'enable_event:sched:sched_switch' > events/sched/sched_process_fork/trigger
( echo "forked")
if [ `cat events/sched/sched_switch/enable` != '1*' ]; then
    fail "enable_event trigger on sched_process_fork did not work"
fi

reset_trigger

echo "Test disable_event trigger"
echo 1 > events/sched/sched_switch/enable
echo 'disable_event:sched:sched_switch' > events/sched/sched_process_fork/trigger
( echo "forked")
if [ `cat events/sched/sched_switch/enable` != '0*' ]; then
    fail "disable_event trigger on sched_process_fork did not work"
fi

reset_trigger

echo "Test semantic error for event enable/disable trigger"
! echo 'enable_event:nogroup:noevent' > events/sched/sched_process_fork/trigger
! echo 'disable_event+1' > events/sched/sched_process_fork/trigger
echo 'enable_event:sched:sched_switch' > events/sched/sched_process_fork/trigger
! echo 'enable_event:sched:sched_switch' > events/sched/sched_process_fork/trigger
! echo 'disable_event:sched:sched_switch' > events/sched/sched_process_fork/trigger

exit 0
