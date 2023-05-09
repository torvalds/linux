#!/bin/sh
# perf record offcpu profiling tests
# SPDX-License-Identifier: GPL-2.0

set -e

err=0
perfdata=$(mktemp /tmp/__perf_test.perf.data.XXXXX)

cleanup() {
  rm -f ${perfdata}
  rm -f ${perfdata}.old
  trap - exit term int
}

trap_cleanup() {
  cleanup
  exit 1
}
trap trap_cleanup exit term int

test_offcpu_priv() {
  echo "Checking off-cpu privilege"

  if [ `id -u` != 0 ]
  then
    echo "off-cpu test [Skipped permission]"
    err=2
    return
  fi
  if perf record --off-cpu -o /dev/null --quiet true 2>&1 | grep BUILD_BPF_SKEL
  then
    echo "off-cpu test [Skipped missing BPF support]"
    err=2
    return
  fi
}

test_offcpu_basic() {
  echo "Basic off-cpu test"

  if ! perf record --off-cpu -e dummy -o ${perfdata} sleep 1 2> /dev/null
  then
    echo "Basic off-cpu test [Failed record]"
    err=1
    return
  fi
  if ! perf evlist -i ${perfdata} | grep -q "offcpu-time"
  then
    echo "Basic off-cpu test [Failed no event]"
    err=1
    return
  fi
  if ! perf report -i ${perfdata} -q --percent-limit=90 | grep -E -q sleep
  then
    echo "Basic off-cpu test [Failed missing output]"
    err=1
    return
  fi
  echo "Basic off-cpu test [Success]"
}

test_offcpu_child() {
  echo "Child task off-cpu test"

  # perf bench sched messaging creates 400 processes
  if ! perf record --off-cpu -e dummy -o ${perfdata} -- \
    perf bench sched messaging -g 10 > /dev/null 2>&1
  then
    echo "Child task off-cpu test [Failed record]"
    err=1
    return
  fi
  if ! perf evlist -i ${perfdata} | grep -q "offcpu-time"
  then
    echo "Child task off-cpu test [Failed no event]"
    err=1
    return
  fi
  # each process waits for read and write, so it should be more than 800 events
  if ! perf report -i ${perfdata} -s comm -q -n -t ';' --percent-limit=90 | \
    awk -F ";" '{ if (NF > 3 && int($3) < 800) exit 1; }'
  then
    echo "Child task off-cpu test [Failed invalid output]"
    err=1
    return
  fi
  echo "Child task off-cpu test [Success]"
}


test_offcpu_priv

if [ $err = 0 ]; then
  test_offcpu_basic
fi

if [ $err = 0 ]; then
  test_offcpu_child
fi

cleanup
exit $err
