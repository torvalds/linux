#!/bin/bash
# perf data type profiling tests
# SPDX-License-Identifier: GPL-2.0

set -e

# The logic below follows the same line as the annotate test, but looks for a
# data type profiling manifestation

# Values in testtypes and testprogs should match
testtypes=("# data-type: struct Buf" "# data-type: struct buf" "# data-type: struct net_conn")
testprogs=("perf test -w code_with_type" "perf test -w datasym" "perf test -w false_sharing")

err=0
perfdata=$(mktemp /tmp/__perf_test.perf.data.XXXXX)
perfout=$(mktemp /tmp/__perf_test.perf.out.XXXXX)

# Check for support of perf mem before trap handler
perf mem record -o /dev/null -- true  2>&1 | \
  		grep -q "failed: no PMU supports the memory events" && exit 2

# Skip if per-thread mem record is not supported on this PMU (e.g. AMD IBS
# needs system-wide '-a'): it is what the test records with below, and a
# failing record must not be reported as a test failure.
if ! perf mem record -o /dev/null -- true 2>/dev/null
then
  echo "Skip: cannot record memory events on this PMU"
  exit 2
fi

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

    "xFS")
    index=2 ;;
  esac

  # Under 'set -e' a bare failing command aborts the script through the EXIT
  # trap, so the commands that report a failure have to be the condition of
  # an 'if' for that reporting to ever happen.
  if [ "x${mode}" == "xBasic" ]
  then
    if ! perf mem record -o "${perfdata}" ${testprogs[$index]} 2> /dev/null
    then
      echo "${mode} annotate [Failed: perf record]"
      err=1
      return
    fi
  else
    if ! perf mem record -o - ${testprogs[$index]} 2> /dev/null > "${perfdata}"
    then
      echo "${mode} annotate [Failed: perf record]"
      err=1
      return
    fi
  fi

  # Generate the annotated output file
  if [ "x${mode}" == "xBasic" ]
  then
    if ! perf annotate --code-with-type -i "${perfdata}" --stdio --percent-limit 1 2> /dev/null > "${perfout}"
    then
      echo "${mode} annotate [Failed: perf annotate]"
      err=1
      return
    fi
  else
    if ! perf annotate --code-with-type -i - --stdio 2> /dev/null --percent-limit 1 < "${perfdata}" > "${perfout}"
    then
      echo "${mode} annotate [Failed: perf annotate]"
      err=1
      return
    fi
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
test_basic_annotate Basic FS
test_basic_annotate Pipe FS

cleanup
exit $err
