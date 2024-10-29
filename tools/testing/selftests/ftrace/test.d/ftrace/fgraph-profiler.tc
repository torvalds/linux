#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: ftrace - function profiler with function graph tracing
# requires: function_profile_enabled set_ftrace_filter function_graph:tracer

# The function graph tracer can now be run along side of the function
# profiler. But there was a bug that caused the combination of the two
# to crash. It also required the function graph tracer to be started
# first.
#
# This test triggers that bug
#
# We need both function_graph and profiling to run this test

fail() { # mesg
    echo $1
    exit_fail
}

echo "Enabling function graph tracer:"
echo function_graph > current_tracer
echo "enable profiler"

# Older kernels do not allow function_profile to be enabled with
# function graph tracer. If the below fails, mark it as unsupported
echo 1 > function_profile_enabled || exit_unsupported

# Let it run for a bit to make sure nothing explodes
sleep 1

exit 0
