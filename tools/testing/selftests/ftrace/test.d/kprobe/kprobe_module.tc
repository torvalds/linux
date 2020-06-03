#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: Kprobe dynamic event - probing module
# requires: kprobe_events

rmmod trace-printk ||:
if ! modprobe trace-printk ; then
  echo "No trace-printk sample module - please make CONFIG_SAMPLE_TRACE_PRINTK=
m"
  exit_unresolved;
fi

MOD=trace_printk
FUNC=trace_printk_irq_work

:;: "Add an event on a module function without specifying event name" ;:

echo "p $MOD:$FUNC" > kprobe_events
PROBE_NAME=`echo $MOD:$FUNC | tr ".:" "_"`
test -d events/kprobes/p_${PROBE_NAME}_0 || exit_failure

:;: "Add an event on a module function with new event name" ;:

echo "p:event1 $MOD:$FUNC" > kprobe_events
test -d events/kprobes/event1 || exit_failure

:;: "Add an event on a module function with new event and group name" ;:

echo "p:kprobes1/event1 $MOD:$FUNC" > kprobe_events
test -d events/kprobes1/event1 || exit_failure

:;: "Remove target module, but event still be there" ;:
if ! rmmod trace-printk ; then
  echo "Failed to unload module - please enable CONFIG_MODULE_UNLOAD"
  exit_unresolved;
fi
test -d events/kprobes1/event1

:;: "Check posibility to defining events on unloaded module";:
echo "p:event2 $MOD:$FUNC" >> kprobe_events

:;: "Target is gone, but we can prepare for next time";:
echo 1 > events/kprobes1/event1/enable

:;: "Load module again, which means the event1 should be recorded";:
modprobe trace-printk
grep "event1:" trace

:;: "Remove the module again and check the event is not locked"
rmmod trace-printk
echo 0 > events/kprobes1/event1/enable
echo "-:kprobes1/event1" >> kprobe_events
