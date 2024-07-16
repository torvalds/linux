#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: ftrace - function trace on module
# requires: set_ftrace_filter

: "mod: allows to filter a non exist function"
echo 'non_exist_func:mod:non_exist_module' > set_ftrace_filter
grep -q "non_exist_func" set_ftrace_filter

: "mod: on exist module"
echo '*:mod:trace_printk' > set_ftrace_filter
if ! modprobe trace-printk ; then
  echo "No trace-printk sample module - please make CONFIG_SAMPLE_TRACE_PRINTK=
m"
  exit_unresolved;
fi

: "Wildcard should be resolved after loading module"
grep -q "trace_printk_irq_work" set_ftrace_filter

: "After removing the filter becomes empty"
rmmod trace_printk
test `cat set_ftrace_filter | wc -l` -eq 0
