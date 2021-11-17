#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: ftrace - Max stack tracer
# requires: stack_trace stack_trace_filter
# Test the basic function of max-stack usage tracing

echo > stack_trace_filter
echo 0 > stack_max_size
echo 1 > /proc/sys/kernel/stack_tracer_enabled

: "Fork and wait for the first entry become !lock"
timeout=10
while [ $timeout -ne 0 ]; do
  ( echo "forked" )
  FL=`grep " 0)" stack_trace`
  echo $FL | grep -q "lock" || break;
  timeout=$((timeout - 1))
done
echo 0 > /proc/sys/kernel/stack_tracer_enabled

echo '*lock*' > stack_trace_filter
test `cat stack_trace_filter | wc -l` -eq `grep lock stack_trace_filter | wc -l`

echo 0 > stack_max_size
echo 1 > /proc/sys/kernel/stack_tracer_enabled

: "Fork and always the first entry including lock"
timeout=10
while [ $timeout -ne 0 ]; do
  ( echo "forked" )
  FL=`grep " 0)" stack_trace`
  echo $FL | grep -q "lock"
  timeout=$((timeout - 1))
done
echo 0 > /proc/sys/kernel/stack_tracer_enabled
