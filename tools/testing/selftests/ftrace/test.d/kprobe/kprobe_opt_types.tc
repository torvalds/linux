#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2023 Akanksha J N, IBM corporation
# description: Register/unregister optimized probe
# requires: kprobe_events

case `uname -m` in
x86_64)
;;
arm*)
;;
ppc*)
;;
*)
  echo "Please implement other architecture here"
  exit_unsupported
esac

DEFAULT=$(cat /proc/sys/debug/kprobes-optimization)
echo 1 > /proc/sys/debug/kprobes-optimization
for i in `seq 0 255`; do
        echo  "p:testprobe $FUNCTION_FORK+${i}" > kprobe_events || continue
        echo 1 > events/kprobes/enable || continue
        (echo "forked")
	PROBE=$(grep $FUNCTION_FORK /sys/kernel/debug/kprobes/list)
        echo 0 > events/kprobes/enable
        echo > kprobe_events
	if echo $PROBE | grep -q OPTIMIZED; then
                echo "$DEFAULT" >  /proc/sys/debug/kprobes-optimization
                exit_pass
        fi
done
echo "$DEFAULT" >  /proc/sys/debug/kprobes-optimization
exit_unresolved
