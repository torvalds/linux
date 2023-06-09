#!/bin/sh
# perf stat tests
# SPDX-License-Identifier: GPL-2.0

set -e

err=0
test_default_stat() {
  echo "Basic stat command test"
  if ! perf stat true 2>&1 | grep -E -q "Performance counter stats for 'true':"
  then
    echo "Basic stat command test [Failed]"
    err=1
    return
  fi
  echo "Basic stat command test [Success]"
}

test_stat_record_report() {
  echo "stat record and report test"
  if ! perf stat record -o - true | perf stat report -i - 2>&1 | \
    grep -E -q "Performance counter stats for 'pipe':"
  then
    echo "stat record and report test [Failed]"
    err=1
    return
  fi
  echo "stat record and report test [Success]"
}

test_stat_record_script() {
  echo "stat record and script test"
  if ! perf stat record -o - true | perf script -i - 2>&1 | \
    grep -E -q "CPU[[:space:]]+THREAD[[:space:]]+VAL[[:space:]]+ENA[[:space:]]+RUN[[:space:]]+TIME[[:space:]]+EVENT"
  then
    echo "stat record and script test [Failed]"
    err=1
    return
  fi
  echo "stat record and script test [Success]"
}

test_stat_repeat_weak_groups() {
  echo "stat repeat weak groups test"
  if ! perf stat -e '{cycles,cycles,cycles,cycles,cycles,cycles,cycles,cycles,cycles,cycles}' \
     true 2>&1 | grep -q 'seconds time elapsed'
  then
    echo "stat repeat weak groups test [Skipped event parsing failed]"
    return
  fi
  if ! perf stat -r2 -e '{cycles,cycles,cycles,cycles,cycles,cycles,cycles,cycles,cycles,cycles}:W' \
    true > /dev/null 2>&1
  then
    echo "stat repeat weak groups test [Failed]"
    err=1
    return
  fi
  echo "stat repeat weak groups test [Success]"
}

test_topdown_groups() {
  # Topdown events must be grouped with the slots event first. Test that
  # parse-events reorders this.
  echo "Topdown event group test"
  if ! perf stat -e '{slots,topdown-retiring}' true > /dev/null 2>&1
  then
    echo "Topdown event group test [Skipped event parsing failed]"
    return
  fi
  if perf stat -e '{slots,topdown-retiring}' true 2>&1 | grep -E -q "<not supported>"
  then
    echo "Topdown event group test [Failed events not supported]"
    err=1
    return
  fi
  if perf stat -e '{topdown-retiring,slots}' true 2>&1 | grep -E -q "<not supported>"
  then
    echo "Topdown event group test [Failed slots not reordered first]"
    err=1
    return
  fi
  echo "Topdown event group test [Success]"
}

test_topdown_weak_groups() {
  # Weak groups break if the perf_event_open of multiple grouped events
  # fails. Breaking a topdown group causes the events to fail. Test a very large
  # grouping to see that the topdown events aren't broken out.
  echo "Topdown weak groups test"
  ok_grouping="{slots,topdown-bad-spec,topdown-be-bound,topdown-fe-bound,topdown-retiring},branch-instructions,branch-misses,bus-cycles,cache-misses,cache-references,cpu-cycles,instructions,mem-loads,mem-stores,ref-cycles,cache-misses,cache-references"
  if ! perf stat --no-merge -e "$ok_grouping" true > /dev/null 2>&1
  then
    echo "Topdown weak groups test [Skipped event parsing failed]"
    return
  fi
  group_needs_break="{slots,topdown-bad-spec,topdown-be-bound,topdown-fe-bound,topdown-retiring,branch-instructions,branch-misses,bus-cycles,cache-misses,cache-references,cpu-cycles,instructions,mem-loads,mem-stores,ref-cycles,cache-misses,cache-references}:W"
  if perf stat --no-merge -e "$group_needs_break" true 2>&1 | grep -E -q "<not supported>"
  then
    echo "Topdown weak groups test [Failed events not supported]"
    err=1
    return
  fi
  echo "Topdown weak groups test [Success]"
}

test_default_stat
test_stat_record_report
test_stat_record_script
test_stat_repeat_weak_groups
test_topdown_groups
test_topdown_weak_groups
exit $err
