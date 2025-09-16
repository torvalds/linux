#!/bin/bash
# perf sched tests
# SPDX-License-Identifier: GPL-2.0

set -e

if [ "$(id -u)" != 0 ]; then
  echo "[Skip] No root permission"
  exit 2
fi

err=0
perfdata=$(mktemp /tmp/__perf_test_sched.perf.data.XXXXX)
PID1=0
PID2=0

cleanup() {
  rm -f "${perfdata}"
  rm -f "${perfdata}".old

  trap - EXIT TERM INT
}

trap_cleanup() {
  echo "Unexpected signal in ${FUNCNAME[1]}"
  cleanup
  exit 1
}
trap trap_cleanup EXIT TERM INT

start_noploops() {
  # Start two noploop workloads on CPU0 to trigger scheduling.
  perf test -w noploop 10 &
  PID1=$!
  taskset -pc 0 $PID1
  perf test -w noploop 10 &
  PID2=$!
  taskset -pc 0 $PID2

  if ! grep -q 'Cpus_allowed_list:\s*0$' "/proc/$PID1/status"
  then
    echo "Sched [Error taskset did not work for the 1st noploop ($PID1)]"
    grep Cpus_allowed /proc/$PID1/status
    err=1
  fi

  if ! grep -q 'Cpus_allowed_list:\s*0$' "/proc/$PID2/status"
  then
    echo "Sched [Error taskset did not work for the 2nd noploop ($PID2)]"
    grep Cpus_allowed /proc/$PID2/status
    err=1
  fi
}

cleanup_noploops() {
  kill "$PID1" "$PID2"
}

test_sched_record() {
  echo "Sched record"

  start_noploops

  perf sched record --no-inherit -o "${perfdata}" sleep 1

  cleanup_noploops
}

test_sched_latency() {
  echo "Sched latency"

  if ! perf sched latency -i "${perfdata}" | grep -q perf-noploop
  then
    echo "Sched latency [Failed missing output]"
    err=1
  fi
}

test_sched_script() {
  echo "Sched script"

  if ! perf sched script -i "${perfdata}" | grep -q perf-noploop
  then
    echo "Sched script [Failed missing output]"
    err=1
  fi
}

test_sched_map() {
  echo "Sched map"

  if ! perf sched map -i "${perfdata}" | grep -q perf-noploop
  then
    echo "Sched map [Failed missing output]"
    err=1
  fi
}

test_sched_timehist() {
  echo "Sched timehist"

  if ! perf sched timehist -i "${perfdata}" | grep -q perf-noploop
  then
    echo "Sched timehist [Failed missing output]"
    err=1
  fi
}

test_sched_record
test_sched_latency
test_sched_script
test_sched_map
test_sched_timehist

cleanup
exit $err
