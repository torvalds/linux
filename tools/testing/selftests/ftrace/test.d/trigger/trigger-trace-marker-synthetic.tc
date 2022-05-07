#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: trace_marker trigger - test histogram with synthetic event
# requires: set_event synthetic_events events/ftrace/print/trigger events/ftrace/print/hist
# flags:

fail() { #msg
    echo $1
    exit_fail
}

echo "Test histogram trace_marker to trace_marker latency histogram trigger"

echo 'latency u64 lat' > synthetic_events
echo 'hist:keys=common_pid:ts0=common_timestamp.usecs if buf == "start"' > events/ftrace/print/trigger
echo 'hist:keys=common_pid:lat=common_timestamp.usecs-$ts0:onmatch(ftrace.print).latency($lat) if buf == "end"' >> events/ftrace/print/trigger
echo 'hist:keys=common_pid,lat:sort=lat' > events/synthetic/latency/trigger
echo -n "start" > trace_marker
echo -n "end" > trace_marker

cnt=`grep 'hitcount: *1$' events/ftrace/print/hist | wc -l`

if [ $cnt -ne 2 ]; then
    fail "hist trace_marker trigger did not trigger correctly"
fi

grep 'hitcount: *1$' events/synthetic/latency/hist > /dev/null || \
    fail "hist trigger did not trigger "

exit 0
