#!/bin/sh
# perf sched stats tests
# SPDX-License-Identifier: GPL-2.0

set -e

err=0
test_perf_sched_stats_record() {
  echo "Basic perf sched stats record test"
  if ! perf sched stats record true 2>&1 | \
    grep -E -q "[ perf sched stats: Wrote samples to perf.data ]"
  then
    echo "Basic perf sched stats record test [Failed]"
    err=1
    return
  fi
  echo "Basic perf sched stats record test [Success]"
}

test_perf_sched_stats_report() {
  echo "Basic perf sched stats report test"
  perf sched stats record true > /dev/null
  if ! perf sched stats report 2>&1 | grep -E -q "Description"
  then
    echo "Basic perf sched stats report test [Failed]"
    err=1
    rm perf.data
    return
  fi
  rm perf.data
  echo "Basic perf sched stats report test [Success]"
}

test_perf_sched_stats_live() {
  echo "Basic perf sched stats live mode test"
  if ! perf sched stats true 2>&1 | grep -E -q "Description"
  then
    echo "Basic perf sched stats live mode test [Failed]"
    err=1
    return
  fi
  echo "Basic perf sched stats live mode test [Success]"
}

test_perf_sched_stats_diff() {
  echo "Basic perf sched stats diff test"
  perf sched stats record true > /dev/null
  perf sched stats record true > /dev/null
  if ! perf sched stats diff > /dev/null
  then
    echo "Basic perf sched stats diff test [Failed]"
    err=1
    rm perf.data.old perf.data
    return
  fi
  rm perf.data.old perf.data
  echo "Basic perf sched stats diff test [Success]"
}

test_perf_sched_stats_record
test_perf_sched_stats_report
test_perf_sched_stats_live
test_perf_sched_stats_diff
exit $err
