#!/bin/sh
# perf diff tests
# SPDX-License-Identifier: GPL-2.0

set -e

err=0
perfdata1=$(mktemp /tmp/__perf_test.perf.data.XXXXX)
perfdata2=$(mktemp /tmp/__perf_test.perf.data.XXXXX)
perfdata3=$(mktemp /tmp/__perf_test.perf.data.XXXXX)
testprog="perf test -w thloop"

shelldir=$(dirname "$0")
# shellcheck source=lib/perf_has_symbol.sh
. "${shelldir}"/lib/perf_has_symbol.sh

testsym="test_loop"

skip_test_missing_symbol ${testsym}

cleanup() {
  rm -rf "${perfdata1}"
  rm -rf "${perfdata1}".old
  rm -rf "${perfdata2}"
  rm -rf "${perfdata2}".old
  rm -rf "${perfdata3}"
  rm -rf "${perfdata3}".old

  trap - EXIT TERM INT
}

trap_cleanup() {
  cleanup
  exit 1
}
trap trap_cleanup EXIT TERM INT

make_data() {
  file="$1"
  if ! perf record -o "${file}" ${testprog} 2> /dev/null
  then
    echo "Workload record [Failed record]" >&2
    echo 1
    return
  fi
  if ! perf report -i "${file}" -q | grep -q "${testsym}"
  then
    echo "Workload record [Failed missing output]" >&2
    echo 1
    return
  fi
  echo 0
}

test_two_files() {
  echo "Basic two file diff test"
  err=$(make_data "${perfdata1}")
  if [ "$err" != 0 ]
  then
    return
  fi
  err=$(make_data "${perfdata2}")
  if [ "$err" != 0 ]
  then
    return
  fi

  if ! perf diff "${perfdata1}" "${perfdata2}" | grep -q "${testsym}"
  then
    echo "Basic two file diff test [Failed diff]"
    err=1
    return
  fi
  echo "Basic two file diff test [Success]"
}

test_three_files() {
  echo "Basic three file diff test"
  err=$(make_data "${perfdata1}")
  if [ "$err" != 0 ]
  then
    return
  fi
  err=$(make_data "${perfdata2}")
  if [ "$err" != 0 ]
  then
    return
  fi
  err=$(make_data "${perfdata3}")
  if [ $err != 0 ]
  then
    return
  fi

  if ! perf diff "${perfdata1}" "${perfdata2}" "${perfdata3}" | grep -q "${testsym}"
  then
    echo "Basic three file diff test [Failed diff]"
    err=1
    return
  fi
  echo "Basic three file diff test [Success]"
}

test_two_files
test_three_files

cleanup
exit $err
