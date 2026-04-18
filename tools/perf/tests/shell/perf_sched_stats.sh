#!/bin/sh
# perf sched stats tests
# SPDX-License-Identifier: GPL-2.0

set -e

if [ "$(id -u)" != 0 ]; then
  echo "[Skip] No root permission"
  exit 2
fi

perfdata=$(mktemp /tmp/__perf_test_sched_stats.perf.data.XXXXX)
perfdata2=$(mktemp /tmp/__perf_test_sched_stats.perf.data.XXXXX)

cleanup() {
  rm -f "${perfdata}"
  rm -f "${perfdata}".old
  rm -f "${perfdata2}"
  rm -f "${perfdata2}".old

  trap - EXIT TERM INT
}

trap_cleanup() {
  echo "Unexpected signal in ${FUNCNAME[1]}"
  cleanup
  exit 1
}
trap trap_cleanup EXIT TERM INT

err=0
test_perf_sched_stats_record() {
  echo "Basic perf sched stats record test"
  if ! perf sched stats record -o "${perfdata}" true 2>&1 | \
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
  perf sched stats record -o "${perfdata}" true > /dev/null
  if ! perf sched stats report -i "${perfdata}" 2>&1 | grep -E -q "Description"
  then
    echo "Basic perf sched stats report test [Failed]"
    err=1
    return
  fi
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
  perf sched stats record -o "${perfdata}" true > /dev/null
  perf sched stats record -o "${perfdata2}" true > /dev/null
  if ! perf sched stats diff "${perfdata}" "${perfdata2}" > /dev/null
  then
    echo "Basic perf sched stats diff test [Failed]"
    err=1
    return
  fi
  echo "Basic perf sched stats diff test [Success]"
}

test_perf_sched_stats_record
test_perf_sched_stats_report
test_perf_sched_stats_live
test_perf_sched_stats_diff

cleanup
exit $err
