#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: ftrace - function trace with cpumask
# requires: function:tracer

if ! which nproc ; then
  nproc() {
    ls -d /sys/devices/system/cpu/cpu[0-9]* | wc -l
  }
fi

NP=`nproc`

if [ $NP -eq 1 ] ;then
  echo "We can not test cpumask on UP environment"
  exit_unresolved
fi

ORIG_CPUMASK=`cat tracing_cpumask`

do_reset() {
  echo $ORIG_CPUMASK > tracing_cpumask
}

echo 0 > tracing_on
echo > trace
: "Bitmask only record on CPU1"
echo 2 > tracing_cpumask
MASK=0x`cat tracing_cpumask`
test `printf "%d" $MASK` -eq 2 || do_reset

echo function > current_tracer
echo 1 > tracing_on
(echo "forked")
echo 0 > tracing_on

: "Check CPU1 events are recorded"
grep -q -e "\[001\]" trace || do_reset

: "There should be No other cpu events"
! grep -qv -e "\[001\]" -e "^#" trace || do_reset

do_reset
