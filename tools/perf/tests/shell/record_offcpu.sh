#!/bin/bash
# perf record offcpu profiling tests (exclusive)
# SPDX-License-Identifier: GPL-2.0

set -e

err=0
perfdata=$(mktemp /tmp/__perf_test.perf.data.XXXXX)

ts=$(printf "%u" $((~0 << 32))) # OFF_CPU_TIMESTAMP
dummy_timestamp=${ts%???} # remove the last 3 digits to match perf script

cleanup() {
  rm -f ${perfdata}
  rm -f ${perfdata}.old
  trap - EXIT TERM INT
}

trap_cleanup() {
  cleanup
  exit 1
}
trap trap_cleanup EXIT TERM INT

test_above_thresh="Threshold test (above threshold)"
test_below_thresh="Threshold test (below threshold)"

test_offcpu_priv() {
  echo "Checking off-cpu privilege"

  if [ "$(id -u)" != 0 ]
  then
    echo "off-cpu test [Skipped permission]"
    err=2
    return
  fi
  if perf version --build-options 2>&1 | grep HAVE_BPF_SKEL | grep -q OFF
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
  # each process waits at least for poll, so it should be more than 400 events
  if ! perf report -i ${perfdata} -s comm -q -n -t ';' --percent-limit=90 | \
    awk -F ";" '{ if (NF > 3 && int($3) < 400) exit 1; }'
  then
    echo "Child task off-cpu test [Failed invalid output]"
    err=1
    return
  fi
  echo "Child task off-cpu test [Success]"
}

# task blocks longer than the --off-cpu-thresh, perf should collect a direct sample
test_offcpu_above_thresh() {
  echo "${test_above_thresh}"

  # collect direct off-cpu samples for tasks blocked for more than 999ms
  if ! perf record -e dummy --off-cpu --off-cpu-thresh 999 -o ${perfdata} -- sleep 1 2> /dev/null
  then
    echo "${test_above_thresh} [Failed record]"
    err=1
    return
  fi
  # direct sample's timestamp should be lower than the dummy_timestamp of the at-the-end sample
  # check if a direct sample exists
  if ! perf script --time "0, ${dummy_timestamp}" -i ${perfdata} -F event | grep -q "offcpu-time"
  then
    echo "${test_above_thresh} [Failed missing direct samples]"
    err=1
    return
  fi
  # there should only be one direct sample, and its period should be higher than off-cpu-thresh
  if ! perf script --time "0, ${dummy_timestamp}" -i ${perfdata} -F period | \
       awk '{ if (int($1) > 999000000) exit 0; else exit 1; }'
  then
    echo "${test_above_thresh} [Failed off-cpu time too short]"
    err=1
    return
  fi
  echo "${test_above_thresh} [Success]"
}

# task blocks shorter than the --off-cpu-thresh, perf should collect an at-the-end sample
test_offcpu_below_thresh() {
  echo "${test_below_thresh}"

  # collect direct off-cpu samples for tasks blocked for more than 1.2s
  if ! perf record -e dummy --off-cpu --off-cpu-thresh 1200 -o ${perfdata} -- sleep 1 2> /dev/null
  then
    echo "${test_below_thresh} [Failed record]"
    err=1
    return
  fi
  # see if there's an at-the-end sample
  if ! perf script --time "${dummy_timestamp}," -i ${perfdata} -F event | grep -q 'offcpu-time'
  then
    echo "${test_below_thresh} [Failed at-the-end samples cannot be found]"
    err=1
    return
  fi
  # plus there shouldn't be any direct samples
  if perf script --time "0, ${dummy_timestamp}" -i ${perfdata} -F event | grep -q 'offcpu-time'
  then
    echo "${test_below_thresh} [Failed direct samples are found when they shouldn't be]"
    err=1
    return
  fi
  echo "${test_below_thresh} [Success]"
}

test_offcpu_priv

if [ $err = 0 ]; then
  test_offcpu_basic
fi

if [ $err = 0 ]; then
  test_offcpu_child
fi

if [ $err = 0 ]; then
  test_offcpu_above_thresh
fi

if [ $err = 0 ]; then
  test_offcpu_below_thresh
fi

cleanup
exit $err
