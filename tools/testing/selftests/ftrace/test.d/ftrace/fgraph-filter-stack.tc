#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: ftrace - function graph filters with stack tracer
# requires: stack_trace set_ftrace_filter function_graph:tracer

# Make sure that function graph filtering works, and is not
# affected by other tracers enabled (like stack tracer)

do_reset() {
    if [ -e /proc/sys/kernel/stack_tracer_enabled ]; then
	    echo 0 > /proc/sys/kernel/stack_tracer_enabled
    fi
}

fail() { # msg
    do_reset
    echo $1
    exit_fail
}

disable_tracing
clear_trace;

# filter something, schedule is always good
if ! echo "schedule" > set_ftrace_filter; then
    # test for powerpc 64
    if ! echo ".schedule" > set_ftrace_filter; then
	fail "can not enable schedule filter"
    fi
fi

echo function_graph > current_tracer

echo "Now testing with stack tracer"

echo 1 > /proc/sys/kernel/stack_tracer_enabled

disable_tracing
clear_trace
enable_tracing
sleep 1

count=`cat trace | grep '()' | grep -v schedule | wc -l`

if [ $count -ne 0 ]; then
    fail "Graph filtering not working with stack tracer?"
fi

# Make sure we did find something
count=`cat trace | grep 'schedule()' | wc -l` 
if [ $count -eq 0 ]; then
    fail "No schedule traces found?"
fi

echo 0 > /proc/sys/kernel/stack_tracer_enabled
clear_trace
sleep 1


count=`cat trace | grep '()' | grep -v schedule | wc -l`

if [ $count -ne 0 ]; then
    fail "Graph filtering not working after stack tracer disabled?"
fi

count=`cat trace | grep 'schedule()' | wc -l` 
if [ $count -eq 0 ]; then
    fail "No schedule traces found?"
fi

do_reset

exit 0
