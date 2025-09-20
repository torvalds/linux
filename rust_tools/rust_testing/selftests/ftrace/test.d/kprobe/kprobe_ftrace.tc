#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: Kprobe dynamic event with function tracer
# requires: kprobe_events stack_trace_filter function:tracer

# prepare
echo nop > current_tracer
echo $FUNCTION_FORK > set_ftrace_filter
echo "p:testprobe $FUNCTION_FORK" > kprobe_events

# kprobe on / ftrace off
echo 1 > events/kprobes/testprobe/enable
echo > trace
( echo "forked")
grep testprobe trace
! grep "$FUNCTION_FORK <-" trace

# kprobe on / ftrace on
echo function > current_tracer
echo > trace
( echo "forked")
grep testprobe trace
grep "$FUNCTION_FORK <-" trace

# kprobe off / ftrace on
echo 0 > events/kprobes/testprobe/enable
echo > trace
( echo "forked")
! grep testprobe trace
grep "$FUNCTION_FORK <-" trace

# kprobe on / ftrace on
echo 1 > events/kprobes/testprobe/enable
echo function > current_tracer
echo > trace
( echo "forked")
grep testprobe trace
grep "$FUNCTION_FORK <-" trace

# kprobe on / ftrace off
echo nop > current_tracer
echo > trace
( echo "forked")
grep testprobe trace
! grep "$FUNCTION_FORK <-" trace
