#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: trace_marker trigger - test histogram trigger
# requires: set_event events/ftrace/print/trigger events/ftrace/print/hist
# flags: instance

fail() { #msg
    echo $1
    exit_fail
}

echo "Test histogram trace_marker trigger"

echo 'hist:keys=common_pid' > events/ftrace/print/trigger
for i in `seq 1 10` ; do echo "hello" > trace_marker; done
grep 'hitcount: *10$' events/ftrace/print/hist > /dev/null || \
    fail "hist trigger did not trigger correct times on trace_marker"

exit 0
