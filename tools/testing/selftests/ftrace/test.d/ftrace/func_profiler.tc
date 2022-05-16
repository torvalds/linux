#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: ftrace - function profiler with function tracing
# requires: function_profile_enabled set_ftrace_filter function_graph:tracer

# There was a bug after a rewrite of the ftrace infrastructure that
# caused the function_profiler not to be able to run with the function
# tracer, because the function_profiler used the function_graph tracer
# and it was assumed the two could not run simultaneously.
#
# There was another related bug where the solution to the first bug
# broke the way filtering of the function tracer worked.
#
# This test triggers those bugs on those kernels.
#
# We need function_graph and profiling to to run this test

fail() { # mesg
    echo $1
    exit_fail
}

echo "Testing function tracer with profiler:"
echo "enable function tracer"
echo function > current_tracer
echo "enable profiler"
echo 1 > function_profile_enabled

sleep 1

echo "Now filter on just schedule"
echo '*schedule' > set_ftrace_filter
clear_trace

echo "Now disable function profiler"
echo 0 > function_profile_enabled

sleep 1

# make sure only schedule functions exist

echo "testing if only schedule is being traced"
if grep -v -e '^#' -e 'schedule' trace; then
	fail "more than schedule was found"
fi

echo "Make sure schedule was traced"
if ! grep -e 'schedule' trace > /dev/null; then
	cat trace
	fail "can not find schedule in trace"
fi

echo > set_ftrace_filter
clear_trace

sleep 1

echo "make sure something other than scheduler is being traced"
if ! grep -v -e '^#' -e 'schedule' trace > /dev/null; then
	cat trace
	fail "no other functions besides schedule was found"
fi

exit 0
