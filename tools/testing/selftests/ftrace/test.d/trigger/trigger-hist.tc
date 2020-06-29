#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: event trigger - test histogram trigger
# requires: set_event events/sched/sched_process_fork/trigger events/sched/sched_process_fork/hist
# flags: instance

fail() { #msg
    echo $1
    exit_fail
}

echo "Test histogram basic trigger"

echo 'hist:keys=parent_pid:vals=child_pid' > events/sched/sched_process_fork/trigger
for i in `seq 1 10` ; do ( echo "forked" > /dev/null); done
grep parent_pid events/sched/sched_process_fork/hist > /dev/null || \
    fail "hist trigger on sched_process_fork did not work"
grep child events/sched/sched_process_fork/hist > /dev/null || \
    fail "hist trigger on sched_process_fork did not work"

reset_trigger

echo "Test histogram with compound keys"

echo 'hist:keys=parent_pid,child_pid' > events/sched/sched_process_fork/trigger
for i in `seq 1 10` ; do ( echo "forked" > /dev/null); done
grep '^{ parent_pid:.*, child_pid:.*}' events/sched/sched_process_fork/hist > /dev/null || \
    fail "compound keys on sched_process_fork did not work"

reset_trigger

echo "Test histogram with string key"

echo 'hist:keys=parent_comm' > events/sched/sched_process_fork/trigger
for i in `seq 1 10` ; do ( echo "forked" > /dev/null); done
COMM=`cat /proc/$$/comm`
grep "parent_comm: $COMM" events/sched/sched_process_fork/hist > /dev/null || \
    fail "string key on sched_process_fork did not work"

reset_trigger

echo "Test histogram with sort key"

echo 'hist:keys=parent_pid,child_pid:sort=child_pid.ascending' > events/sched/sched_process_fork/trigger
for i in `seq 1 10` ; do ( echo "forked" > /dev/null); done

check_inc() {
    while [ $# -gt 1 ]; do
        [ $1 -gt $2 ] && return 1
        shift 1
    done
    return 0
}
check_inc `grep -o "child_pid:[[:space:]]*[[:digit:]]*" \
    events/sched/sched_process_fork/hist | cut -d: -f2 ` ||
    fail "sort param on sched_process_fork did not work"

exit 0
