#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: Test trace_printk from module

rmmod trace-printk ||:
if ! modprobe trace-printk ; then
  echo "No trace-printk sample module - please make CONFIG_SAMPLE_TRACE_PRINTK=m"
  exit_unresolved;
fi

echo "Waiting for irq work"
sleep 1

grep -q ": This .* trace_bputs" trace
grep -q ": This .* trace_puts" trace
grep -q ": This .* trace_bprintk" trace
grep -q ": This .* trace_printk" trace

grep -q ": (irq) .* trace_bputs" trace
grep -q ": (irq) .* trace_puts" trace
grep -q ": (irq) .* trace_bprintk" trace
grep -q ": (irq) .* trace_printk" trace

grep -q "This is a %s that will use trace_bprintk" printk_formats
grep -q "(irq) This is a static string that will use trace_bputs" printk_formats

rmmod trace-printk ||:
