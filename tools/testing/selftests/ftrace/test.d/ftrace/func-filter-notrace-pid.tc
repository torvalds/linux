#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: ftrace - function pid notrace filters
# requires: set_ftrace_notrace_pid set_ftrace_filter function:tracer
# flags: instance

# Make sure that function pid matching filter with notrace works.

do_function_fork=1

if [ ! -f options/function-fork ]; then
    do_function_fork=0
    echo "no option for function-fork found. Option will not be tested."
fi

read PID _ < /proc/self/stat

if [ $do_function_fork -eq 1 ]; then
    # default value of function-fork option
    orig_value=`grep function-fork trace_options`
fi

do_reset() {
    if [ $do_function_fork -eq 0 ]; then
	return
    fi

    echo > set_ftrace_notrace_pid
    echo $orig_value > trace_options
}

fail() { # msg
    do_reset
    echo $1
    exit_fail
}

do_test() {
    disable_tracing

    echo do_execve* > set_ftrace_filter
    echo $FUNCTION_FORK >> set_ftrace_filter

    echo $PID > set_ftrace_notrace_pid
    echo function > current_tracer

    if [ $do_function_fork -eq 1 ]; then
	# don't allow children to be traced
	echo nofunction-fork > trace_options
    fi

    enable_tracing
    yield

    count_pid=`cat trace | grep -v ^# | grep $PID | wc -l`
    count_other=`cat trace | grep -v ^# | grep -v $PID | wc -l`

    # count_pid should be 0
    if [ $count_pid -ne 0 -o $count_other -eq 0 ]; then
	fail "PID filtering not working? traced task = $count_pid; other tasks = $count_other "
    fi

    disable_tracing
    clear_trace

    if [ $do_function_fork -eq 0 ]; then
	return
    fi

    # allow children to be traced
    echo function-fork > trace_options

    # With pid in both set_ftrace_pid and set_ftrace_notrace_pid
    # there should not be any tasks traced.

    echo $PID > set_ftrace_pid

    enable_tracing
    yield

    count_pid=`cat trace | grep -v ^# | grep $PID | wc -l`
    count_other=`cat trace | grep -v ^# | grep -v $PID | wc -l`

    # both should be zero
    if [ $count_pid -ne 0 -o $count_other -ne 0 ]; then
	fail "PID filtering not following fork? traced task = $count_pid; other tasks = $count_other "
    fi
}

do_test

do_reset

exit 0
