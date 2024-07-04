#!/bin/sh
# perf annotate basic tests
# SPDX-License-Identifier: GPL-2.0

set -e

shelldir=$(dirname "$0")

# shellcheck source=lib/perf_has_symbol.sh
. "${shelldir}"/lib/perf_has_symbol.sh

testsym="noploop"

skip_test_missing_symbol ${testsym}

err=0
perfdata=$(mktemp /tmp/__perf_test.perf.data.XXXXX)
testprog="perf test -w noploop"
# disassembly format: "percent : offset: instruction (operands ...)"
disasm_regex="[0-9]*\.[0-9]* *: *\w*: *\w*"

cleanup() {
  rm -rf "${perfdata}"
  rm -rf "${perfdata}".old

  trap - EXIT TERM INT
}

trap_cleanup() {
  cleanup
  exit 1
}
trap trap_cleanup EXIT TERM INT

test_basic() {
  echo "Basic perf annotate test"
  if ! perf record -o "${perfdata}" ${testprog} 2> /dev/null
  then
    echo "Basic annotate [Failed: perf record]"
    err=1
    return
  fi

  # check if it has the target symbol
  if ! perf annotate -i "${perfdata}" 2> /dev/null | grep "${testsym}"
  then
    echo "Basic annotate [Failed: missing target symbol]"
    err=1
    return
  fi

  # check if it has the disassembly lines
  if ! perf annotate -i "${perfdata}" 2> /dev/null | grep "${disasm_regex}"
  then
    echo "Basic annotate [Failed: missing disasm output from default disassembler]"
    err=1
    return
  fi

  # check again with a target symbol name
  if ! perf annotate -i "${perfdata}" "${testsym}" 2> /dev/null | \
	  grep -m 3 "${disasm_regex}"
  then
    echo "Basic annotate [Failed: missing disasm output when specifying the target symbol]"
    err=1
    return
  fi

  # check one more with external objdump tool (forced by --objdump option)
  if ! perf annotate -i "${perfdata}" --objdump=objdump 2> /dev/null | \
	  grep -m 3 "${disasm_regex}"
  then
    echo "Basic annotate [Failed: missing disasm output from non default disassembler (using --objdump)]"
    err=1
    return
  fi
  echo "Basic annotate test [Success]"
}

test_basic

cleanup
exit $err
