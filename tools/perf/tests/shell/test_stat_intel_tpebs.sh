#!/bin/bash
# test Intel TPEBS counting mode (exclusive)
# SPDX-License-Identifier: GPL-2.0

set -e

ParanoidAndNotRoot() {
  [ "$(id -u)" != 0 ] && [ "$(cat /proc/sys/kernel/perf_event_paranoid)" -gt $1 ]
}

if ! grep -q GenuineIntel /proc/cpuinfo
then
  echo "Skipping non-Intel"
  exit 2
fi

if ParanoidAndNotRoot 0
then
  echo "Skipping paranoid >0 and not root"
  exit 2
fi

stat_output=$(mktemp /tmp/__perf_stat_tpebs_output.XXXXX)

cleanup() {
  rm -rf "${stat_output}"
  trap - EXIT TERM INT
}

trap_cleanup() {
  echo "Unexpected signal in ${FUNCNAME[1]}"
  cat "${stat_output}"
  cleanup
  exit 1
}
trap trap_cleanup EXIT TERM INT

# Event to be used in tests
event=cache-misses

if ! perf record -e "${event}:p" -a -o /dev/null sleep 0.01 > "${stat_output}" 2>&1
then
  echo "Missing ${event} support"
  cleanup
  exit 2
fi

test_with_record_tpebs() {
  echo "Testing with --record-tpebs"
  if ! perf stat -e "${event}:R" --record-tpebs -a sleep 0.01 > "${stat_output}" 2>&1
  then
    echo "Testing with --record-tpebs [Failed perf stat]"
    cat "${stat_output}"
    exit 1
  fi

  # Expected output:
  # $ perf stat --record-tpebs -e cache-misses:R -a sleep 0.01
  # Events enabled
  # [ perf record: Woken up 2 times to write data ]
  # [ perf record: Captured and wrote 0.056 MB - ]
  #
  #  Performance counter stats for 'system wide':
  #
  #                  0      cache-misses:R
  #
  #        0.013963299 seconds time elapsed
  if ! grep "perf record" "${stat_output}"
  then
    echo "Testing with --record-tpebs [Failed missing perf record]"
    cat "${stat_output}"
    exit 1
  fi
  if ! grep "${event}:R" "${stat_output}" && ! grep "/${event}/R" "${stat_output}"
  then
    echo "Testing with --record-tpebs [Failed missing event name]"
    cat "${stat_output}"
    exit 1
  fi
  echo "Testing with --record-tpebs [Success]"
}

test_with_record_tpebs
cleanup
exit 0
