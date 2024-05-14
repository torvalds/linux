#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: event trigger - test inter-event histogram trigger trace action with dynamic string param
# requires: set_event synthetic_events events/sched/sched_process_exec/hist "char name[]' >> synthetic_events":README

fail() { #msg
    echo $1
    exit_fail
}

echo "Test create synthetic event"

echo 'ping_test_latency u64 lat; char filename[]' > synthetic_events
if [ ! -d events/synthetic/ping_test_latency ]; then
    fail "Failed to create ping_test_latency synthetic event"
fi

echo "Test create histogram for synthetic event using trace action and dynamic strings"
echo "Test histogram dynamic string variables,simple expression support and trace action"

echo 'hist:key=pid:filenamevar=filename:ts0=common_timestamp.usecs' > events/sched/sched_process_exec/trigger
echo 'hist:key=pid:lat=common_timestamp.usecs-$ts0:onmatch(sched.sched_process_exec).ping_test_latency($lat,$filenamevar) if comm == "ping"' > events/sched/sched_process_exit/trigger
echo 'hist:keys=filename,lat:sort=filename,lat' > events/synthetic/ping_test_latency/trigger

ping $LOCALHOST -c 5

if ! grep -q "ping" events/synthetic/ping_test_latency/hist; then
    fail "Failed to create dynamic string trace action inter-event histogram"
fi

exit 0
