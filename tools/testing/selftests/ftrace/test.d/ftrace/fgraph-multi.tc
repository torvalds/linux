#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: ftrace - function graph filters
# requires: set_ftrace_filter function_graph:tracer

# Make sure that function graph filtering works

INSTANCE1="instances/test1_$$"
INSTANCE2="instances/test2_$$"
INSTANCE3="instances/test3_$$"

WD=`pwd`

do_reset() {
    cd $WD
    if [ -d $INSTANCE1 ]; then
	echo nop > $INSTANCE1/current_tracer
	rmdir $INSTANCE1
    fi
    if [ -d $INSTANCE2 ]; then
	echo nop > $INSTANCE2/current_tracer
	rmdir $INSTANCE2
    fi
    if [ -d $INSTANCE3 ]; then
	echo nop > $INSTANCE3/current_tracer
	rmdir $INSTANCE3
    fi
}

mkdir $INSTANCE1
if ! grep -q function_graph $INSTANCE1/available_tracers; then
    echo "function_graph not allowed with instances"
    rmdir $INSTANCE1
    exit_unsupported
fi

mkdir $INSTANCE2
mkdir $INSTANCE3

fail() { # msg
    do_reset
    echo $1
    exit_fail
}

disable_tracing
clear_trace

do_test() {
    REGEX=$1
    TEST=$2

    # filter something, schedule is always good
    if ! echo "$REGEX" > set_ftrace_filter; then
	fail "can not enable filter $REGEX"
    fi

    echo > trace
    echo function_graph > current_tracer
    enable_tracing
    sleep 1
    # search for functions (has "{" or ";" on the line)
    echo 0 > tracing_on
    count=`cat trace | grep -v '^#' | grep -e '{' -e ';' | grep -v "$TEST" | wc -l`
    echo 1 > tracing_on
    if [ $count -ne 0 ]; then
	fail "Graph filtering not working by itself against $TEST?"
    fi

    # Make sure we did find something
    echo 0 > tracing_on
    count=`cat trace | grep -v '^#' | grep -e '{' -e ';' | grep "$TEST" | wc -l`
    echo 1 > tracing_on
    if [ $count -eq 0 ]; then
	fail "No traces found with $TEST?"
    fi
}

do_test '*sched*' 'sched'
cd $INSTANCE1
do_test '*lock*' 'lock'
cd $WD
cd $INSTANCE2
do_test '*rcu*' 'rcu'
cd $WD
cd $INSTANCE3
echo function_graph > current_tracer

sleep 1
count=`cat trace | grep -v '^#' | grep -e '{' -e ';' | grep "$TEST" | wc -l`
if [ $count -eq 0 ]; then
    fail "No traces found with all tracing?"
fi

cd $WD
echo nop > current_tracer
echo nop > $INSTANCE1/current_tracer
echo nop > $INSTANCE2/current_tracer
echo nop > $INSTANCE3/current_tracer

do_reset

exit 0
