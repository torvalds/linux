#!/bin/bash
# perf data type profiling tests
# SPDX-License-Identifier: GPL-2.0

set -e

# The logic below follows the same line as the annotate test, but looks for a
# data type profiling manifestation

# Values in testtypes and testprogs should match
testtypes=("# data-type: struct Buf" "# data-type: struct _buf")
testprogs=("perf test -w code_with_type" "perf test -w datasym")

err=0
perfdata=$(mktemp /tmp/__perf_test.perf.data.XXXXX)
perfout=$(mktemp /tmp/__perf_test.perf.out.XXXXX)

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
  runtime=$2

  echo "${mode} ${runtime} perf annotate test"

  case "x${runtime}" in
    "xRust")
    if ! perf check feature -q rust
    then
      echo "Skip: code_with_type workload not built in 'perf test'"
      return
    fi
    index=0 ;;

    "xC")
    index=1 ;;
  esac

  if [ "x${mode}" == "xBasic" ]
  then
    perf mem record -o "${perfdata}" ${testprogs[$index]} 2> /dev/null
  else
    perf mem record -o - ${testprogs[$index]} 2> /dev/null > "${perfdata}"
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
  if ! grep -q "${testtypes[$index]}" "${perfout}"
  then
    echo "${mode} annotate [Failed: missing target data type]"
    cat "${perfout}"
    err=1
    return
  fi
  echo "${mode} annotate test [Success]"
}

test_basic_annotate Basic Rust
test_basic_annotate Pipe Rust
test_basic_annotate Basic C
test_basic_annotate Pipe C

cleanup
exit $err
