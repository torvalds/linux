#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: ftrace - function pid filters
# requires: set_ftrace_pid set_ftrace_filter function:tracer
# flags: instance

# Make sure that function pid matching filter works.
# Also test it on an instance directory

do_function_fork=1
do_funcgraph_proc=1

if [ ! -f options/function-fork ]; then
    do_function_fork=0
    echo "no option for function-fork found. Option will not be tested."
fi

if [ ! -f options/funcgraph-proc ]; then
    do_funcgraph_proc=0
    echo "no option for function-fork found. Option will not be tested."
fi

read PID _ < /proc/self/stat

if [ $do_function_fork -eq 1 ]; then
    # default value of function-fork option
    orig_value=`grep function-fork trace_options`
fi

if [ $do_funcgraph_proc -eq 1 ]; then
    orig_value2=`cat options/funcgraph-proc`
    echo 1 > options/funcgraph-proc
fi

do_reset() {
    if [ $do_function_fork -eq 1 ]; then
	echo $orig_value > trace_options
    fi

    if [ $do_funcgraph_proc -eq 1 ]; then
	echo $orig_value2 > options/funcgraph-proc
    fi
}

fail() { # msg
    do_reset
    echo $1
    exit_fail
}

do_test() {
    TRACER=$1

    disable_tracing

    echo do_execve* > set_ftrace_filter
    echo $FUNCTION_FORK >> set_ftrace_filter

    echo $PID > set_ftrace_pid
    echo $TRACER > current_tracer

    if [ $do_function_fork -eq 1 ]; then
	# don't allow children to be traced
	echo nofunction-fork > trace_options
    fi

    enable_tracing
    yield

    count_pid=`cat trace | grep -v ^# | grep $PID | wc -l`
    count_other=`cat trace | grep -v ^# | grep -v $PID | wc -l`

    # count_other should be 0
    if [ $count_pid -eq 0 -o $count_other -ne 0 ]; then
	fail "PID filtering not working?"
    fi

    disable_tracing
    clear_trace

    if [ $do_function_fork -eq 0 ]; then
	return
    fi

    # allow children to be traced
    echo function-fork > trace_options

    enable_tracing
    yield

    count_pid=`cat trace | grep -v ^# | grep $PID | wc -l`
    count_other=`cat trace | grep -v ^# | grep -v $PID | wc -l`

    # count_other should NOT be 0
    if [ $count_pid -eq 0 -o $count_other -eq 0 ]; then
	fail "PID filtering not following fork?"
    fi
}

do_test function
if grep -s function_graph available_tracers; then
    do_test function_graph
fi

do_reset

exit 0
