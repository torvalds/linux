#!/bin/bash
# perf data type profiling tests
# SPDX-License-Identifier: GPL-2.0

set -e

# The logic below follows the same line as the annotate test, but looks for a
# data type profiling manifestation
testtype="# data-type: struct Buf"

err=0
perfdata=$(mktemp /tmp/__perf_test.perf.data.XXXXX)
perfout=$(mktemp /tmp/__perf_test.perf.out.XXXXX)
testprog="perf test -w code_with_type"

cleanup() {
  rm -rf "${perfdata}" "${perfout}"
  rm -rf "${perfdata}".old

  trap - EXIT TERM INT
}

trap_cleanup() {
  echo "Unexpected signal in ${FUNCNAME[1]}"
  cleanup
  exit 1
}
trap trap_cleanup EXIT TERM INT

test_basic_annotate() {
  mode=$1
  echo "${mode} perf annotate test"
  if [ "x${mode}" == "xBasic" ]
  then
    perf mem record -o "${perfdata}" ${testprog} 2> /dev/null
  else
    perf mem record -o - ${testprog} 2> /dev/null > "${perfdata}"
  fi
  if [ "x$?" != "x0" ]
  then
    echo "${mode} annotate [Failed: perf record]"
    err=1
    return
  fi

  # Generate the annotated output file
  if [ "x${mode}" == "xBasic" ]
  then
    perf annotate --code-with-type -i "${perfdata}" --stdio --percent-limit 1 2> /dev/null > "${perfout}"
  else
    perf annotate --code-with-type -i - --stdio 2> /dev/null --percent-limit 1 < "${perfdata}" > "${perfout}"
  fi

  # check if it has the target data type
  if ! grep -q "${testtype}" "${perfout}"
  then
    echo "${mode} annotate [Failed: missing target data type]"
    cat "${perfout}"
    err=1
    return
  fi
  echo "${mode} annotate test [Success]"
}

test_basic_annotate Basic
test_basic_annotate Pipe

cleanup
exit $err
