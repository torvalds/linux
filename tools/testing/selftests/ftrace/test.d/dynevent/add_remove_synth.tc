#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: Generic dynamic event - add/remove synthetic events
# requires: dynamic_events "s:[synthetic/]":README

echo 0 > events/enable
echo > dynamic_events

echo "s:latency1 u64 lat; pid_t pid;" >> dynamic_events
echo "s:latency2 u64 lat; pid_t pid;" >> dynamic_events

grep -q latency1 dynamic_events
grep -q latency2 dynamic_events
test -d events/synthetic/latency1
test -d events/synthetic/latency2

echo "-:synthetic/latency2" >> dynamic_events

grep -q latency1 dynamic_events
! grep -q latency2 dynamic_events

echo > dynamic_events

clear_trace
