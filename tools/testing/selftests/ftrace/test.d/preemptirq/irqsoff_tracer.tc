#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: test for the preemptirqsoff tracer
# requires: preemptoff:tracer irqsoff:tracer

MOD=preemptirq_delay_test

fail() {
    reset_tracer
    rmmod $MOD || true
    exit_fail
}

unsup() { #msg
    reset_tracer
    rmmod $MOD || true
    echo $1
    exit_unsupported
}

unres() { #msg
    reset_tracer
    rmmod $MOD || true
    echo $1
    exit_unresolved
}

modprobe $MOD || unres "$MOD module not available"
rmmod $MOD

reset_tracer

# Simulate preemptoff section for half a second couple of times
echo preemptoff > current_tracer
sleep 1
modprobe $MOD test_mode=preempt delay=500000 || fail
rmmod $MOD || fail
modprobe $MOD test_mode=preempt delay=500000 || fail
rmmod $MOD || fail
modprobe $MOD test_mode=preempt delay=500000 || fail
rmmod $MOD || fail

cat trace

# Confirm which tracer
grep -q "tracer: preemptoff" trace || fail

# Check the end of the section
egrep -q "5.....us : <stack trace>" trace || fail

# Check for 500ms of latency
egrep -q "latency: 5..... us" trace || fail

reset_tracer

# Simulate irqsoff section for half a second couple of times
echo irqsoff > current_tracer
sleep 1
modprobe $MOD test_mode=irq delay=500000 || fail
rmmod $MOD || fail
modprobe $MOD test_mode=irq delay=500000 || fail
rmmod $MOD || fail
modprobe $MOD test_mode=irq delay=500000 || fail
rmmod $MOD || fail

cat trace

# Confirm which tracer
grep -q "tracer: irqsoff" trace || fail

# Check the end of the section
egrep -q "5.....us : <stack trace>" trace || fail

# Check for 500ms of latency
egrep -q "latency: 5..... us" trace || fail

reset_tracer
exit 0
