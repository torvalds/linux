#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: Snapshot and tracing_cpumask
# requires: trace_marker tracing_cpumask snapshot
# flags: instance

# This testcase is constrived to reproduce a problem that the cpu buffers
# become unavailable which is due to 'record_disabled' of array_buffer and
# max_buffer being messed up.

# Store origin cpumask
ORIG_CPUMASK=`cat tracing_cpumask`

# Stop tracing all cpu
echo 0 > tracing_cpumask

# Take a snapshot of the main buffer
echo 1 > snapshot

# Restore origin cpumask, note that there should be some cpus being traced
echo ${ORIG_CPUMASK} > tracing_cpumask

# Set tracing on
echo 1 > tracing_on

# Write a log into buffer
echo "test input 1" > trace_marker

# Ensure the log writed so that cpu buffers are still available
grep -q "test input 1" trace
exit 0
