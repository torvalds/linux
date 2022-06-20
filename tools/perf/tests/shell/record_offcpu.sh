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

test_offcpu() {
  echo "Basic off-cpu test"
  if [ `id -u` != 0 ]
  then
    echo "Basic off-cpu test [Skipped permission]"
    err=2
    return
  fi
  if perf record --off-cpu -o ${perfdata} --quiet true 2>&1 | grep BUILD_BPF_SKEL
  then
    echo "Basic off-cpu test [Skipped missing BPF support]"
    err=2
    return
  fi
  if ! perf record --off-cpu -e dummy -o ${perfdata} sleep 1 2> /dev/null
  then
    echo "Basic off-cpu test [Failed record]"
    err=1
    return
  fi
  if ! perf evlist -i ${perfdata} | grep -q "offcpu-time"
  then
    echo "Basic off-cpu test [Failed record]"
    err=1
    return
  fi
  if ! perf report -i ${perfdata} -q --percent-limit=90 | egrep -q sleep
  then
    echo "Basic off-cpu test [Failed missing output]"
    err=1
    return
  fi
  echo "Basic off-cpu test [Success]"
}

test_offcpu

cleanup
exit $err
