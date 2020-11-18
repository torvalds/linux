#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: Test wakeup RT tracer
# requires: wakeup_rt:tracer

if ! which chrt ; then
  echo "chrt is not found. This test requires chrt command."
  exit_unresolved
fi

echo wakeup_rt > current_tracer
echo 1 > tracing_on
echo 0 > tracing_max_latency

: "Wakeup a realtime task"
chrt -f 5 sleep 1

echo 0 > tracing_on
grep "+ \[[[:digit:]]*\]" trace
grep "==> \[[[:digit:]]*\]" trace

