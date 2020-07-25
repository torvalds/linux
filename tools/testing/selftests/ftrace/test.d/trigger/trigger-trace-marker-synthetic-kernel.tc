#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: trace_marker trigger - test histogram with synthetic event against kernel event
# requires: set_event synthetic_events events/sched/sched_waking events/ftrace/print/trigger events/ftrace/print/hist
# flags:

fail() { #msg
    echo $1
    exit_fail
}

echo "Test histogram kernel event to trace_marker latency histogram trigger"

echo 'latency u64 lat' > synthetic_events
echo 'hist:keys=pid:ts0=common_timestamp.usecs' > events/sched/sched_waking/trigger
echo 'hist:keys=common_pid:lat=common_timestamp.usecs-$ts0:onmatch(sched.sched_waking).latency($lat)' > events/ftrace/print/trigger
echo 'hist:keys=common_pid,lat:sort=lat' > events/synthetic/latency/trigger
sleep 1
echo "hello" > trace_marker

grep 'hitcount: *1$' events/ftrace/print/hist > /dev/null || \
    fail "hist trigger did not trigger correct times on trace_marker"

grep 'hitcount: *1$' events/synthetic/latency/hist > /dev/null || \
    fail "hist trigger did not trigger "

exit 0
