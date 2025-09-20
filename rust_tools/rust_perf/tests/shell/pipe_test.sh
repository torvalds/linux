#!/bin/bash
# perf pipe recording and injection test
# SPDX-License-Identifier: GPL-2.0

shelldir=$(dirname "$0")
# shellcheck source=lib/perf_has_symbol.sh
. "${shelldir}"/lib/perf_has_symbol.sh

sym="noploop"

skip_test_missing_symbol ${sym}

data=$(mktemp /tmp/perf.data.XXXXXX)
data2=$(mktemp /tmp/perf.data2.XXXXXX)
prog="perf test -w noploop"
[ "$(uname -m)" = "s390x" ] && prog="$prog 3"
err=0

set -e

cleanup() {
  rm -rf "${data}"
  rm -rf "${data}".old
  rm -rf "${data2}"
  rm -rf "${data2}".old

  trap - EXIT TERM INT
}

trap_cleanup() {
  echo "Unexpected signal in ${FUNCNAME[1]}"
  cleanup
  exit 1
}
trap trap_cleanup EXIT TERM INT

test_record_report() {
  echo
  echo "Record+report pipe test"

  task="perf"
  if ! perf record -e task-clock:u -o - ${prog} | perf report -i - --task | grep -q ${task}
  then
    echo "Record+report pipe test [Failed - cannot find the test file in the perf report #1]"
    err=1
    return
  fi

  if ! perf record -g -e task-clock:u -o - ${prog} | perf report -i - --task | grep -q ${task}
  then
    echo "Record+report pipe test [Failed - cannot find the test file in the perf report #2]"
    err=1
    return
  fi

  perf record -g -e task-clock:u -o - ${prog} > ${data}
  if ! perf report -i ${data} --task | grep -q ${task}
  then
    echo "Record+report pipe test [Failed - cannot find the test file in the perf report #3]"
    err=1
    return
  fi

  echo "Record+report pipe test [Success]"
}

test_inject_bids() {
  inject_opt=$1

  echo
  echo "Inject ${inject_opt} build-ids test"

  if ! perf record -e task-clock:u -o - ${prog} | perf inject ${inject_opt}| perf report -i - | grep -q ${sym}
  then
    echo "Inject build-ids test [Failed - cannot find noploop function in pipe #1]"
    err=1
    return
  fi

  if ! perf record -g -e task-clock:u -o - ${prog} | perf inject ${inject_opt} | perf report -i - | grep -q ${sym}
  then
    echo "Inject ${inject_opt} build-ids test [Failed - cannot find noploop function in pipe #2]"
    err=1
    return
  fi

  perf record -e task-clock:u -o - ${prog} | perf inject ${inject_opt} -o ${data}
  if ! perf report -i ${data} | grep -q ${sym}; then
    echo "Inject ${inject_opt} build-ids test [Failed - cannot find noploop function in pipe #3]"
    err=1
    return
  fi

  perf record -e task-clock:u -o ${data} ${prog}
  if ! perf inject ${inject_opt} -i ${data} | perf report -i - | grep -q ${sym}; then
    echo "Inject ${inject_opt} build-ids test [Failed - cannot find noploop function in pipe #4]"
    err=1
    return
  fi

  perf record -e task-clock:u -o - ${prog} > ${data}
  if ! perf inject ${inject_opt} -i ${data} | perf report -i - | grep -q ${sym}; then
    echo "Inject ${inject_opt} build-ids test [Failed - cannot find noploop function in pipe #5]"
    err=1
    return
  fi

  perf record -e task-clock:u -o - ${prog} > ${data}
  perf inject ${inject_opt} -i ${data} -o ${data2}
  if ! perf report -i ${data2} | grep -q ${sym}; then
    echo "Inject ${inject_opt} build-ids test [Failed - cannot find noploop function in pipe #6]"
    err=1
    return
  fi

  echo "Inject ${inject_opt} build-ids test [Success]"
}

test_record_report
test_inject_bids -B
test_inject_bids -b
test_inject_bids --buildid-all
test_inject_bids --mmap2-buildid-all

cleanup
exit $err

