#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# description: ftrace - function trace across cpu hotplug
# requires: function:tracer

if ! which nproc ; then
  nproc() {
    ls -d /sys/devices/system/cpu/cpu[0-9]* | wc -l
  }
fi

NP=`nproc`

if [ $NP -eq 1 ] ;then
  echo "We cannot test cpu hotplug in UP environment"
  exit_unresolved
fi

# Find online cpu
for i in /sys/devices/system/cpu/cpu[1-9]*; do
	if [ -f $i/online ] && [ "$(cat $i/online)" = "1" ]; then
		cpu=$i
		break
	fi
done

if [ -z "$cpu" ]; then
	echo "We cannot test cpu hotplug with a single cpu online"
	exit_unresolved
fi

echo 0 > tracing_on
echo > trace

: "Set $(basename $cpu) offline/online with function tracer enabled"
echo function > current_tracer
echo 1 > tracing_on
(echo 0 > $cpu/online)
(echo "forked"; sleep 1)
(echo 1 > $cpu/online)
echo 0 > tracing_on
echo nop > current_tracer
