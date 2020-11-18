#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: Test creation and deletion of trace instances while setting an event
# requires: instances

fail() { # mesg
    rmdir foo 2>/dev/null
    echo $1
    set -e
    exit_fail
}

cd instances

# we don't want to fail on error
set +e

mkdir x
rmdir x
result=$?

if [ $result -ne 0 ]; then
    echo "instance rmdir not supported"
    exit_unsupported
fi

instance_slam() {
        while :; do
                mkdir foo 2> /dev/null
                rmdir foo 2> /dev/null
        done
}

instance_read() {
        while :; do
                cat foo/trace 1> /dev/null 2>&1
        done
}

instance_set() {
        while :; do
                echo 1 > foo/events/sched/sched_switch
        done 2> /dev/null
}

instance_slam &
p1=$!
echo $p1

instance_set &
p2=$!
echo $p2

instance_read &
p3=$!
echo $p3

sleep 1

kill -1 $p3
kill -1 $p2
kill -1 $p1

echo "Wait for processes to finish"
wait $p1 $p2 $p3
echo "all processes finished, wait for cleanup"
sleep 1

mkdir foo
ls foo > /dev/null
rmdir foo
if [ -d foo ]; then
        fail "foo still exists"
fi

mkdir foo
echo "schedule:enable_event:sched:sched_switch" > foo/set_ftrace_filter
rmdir foo
if [ -d foo ]; then
        fail "foo still exists"
fi
if grep -q "schedule:enable_event:sched:sched_switch" ../set_ftrace_filter; then
	echo "Older kernel detected. Cleanup filter"
	echo '!schedule:enable_event:sched:sched_switch' > ../set_ftrace_filter
fi

instance_slam() {
    while :; do
	mkdir x
	mkdir y
	mkdir z
	rmdir x
	rmdir y
	rmdir z
    done 2>/dev/null
}

instance_slam &
p1=$!
echo $p1

instance_slam &
p2=$!
echo $p2

instance_slam &
p3=$!
echo $p3

instance_slam &
p4=$!
echo $p4

instance_slam &
p5=$!
echo $p5

ls -lR >/dev/null
sleep 1

kill -1 $p1
kill -1 $p2
kill -1 $p3
kill -1 $p4
kill -1 $p5

echo "Wait for processes to finish"
wait $p1 $p2 $p3 $p4 $p5
echo "all processes finished, wait for cleanup"

mkdir x y z
ls x y z
rmdir x y z
for d in x y z; do
        if [ -d $d ]; then
                fail "instance $d still exists"
        fi
done

set -e

exit 0
