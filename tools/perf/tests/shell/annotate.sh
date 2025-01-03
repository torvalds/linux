#!/bin/bash
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
perfout=$(mktemp /tmp/__perf_test.perf.out.XXXXX)
testprog="perf test -w noploop"
# disassembly format: "percent : offset: instruction (operands ...)"
disasm_regex="[0-9]*\.[0-9]* *: *\w*: *\w*"

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

test_basic() {
  echo "Basic perf annotate test"
  if ! perf record -o "${perfdata}" ${testprog} 2> /dev/null
  then
    echo "Basic annotate [Failed: perf record]"
    err=1
    return
  fi

  # Generate the annotated output file
  perf annotate --no-demangle -i "${perfdata}" --stdio 2> /dev/null | head -250 > "${perfout}"

  # check if it has the target symbol
  if ! grep "${testsym}" "${perfout}"
  then
    echo "Basic annotate [Failed: missing target symbol]"
    err=1
    return
  fi

  # check if it has the disassembly lines
  if ! grep "${disasm_regex}" "${perfout}"
  then
    echo "Basic annotate [Failed: missing disasm output from default disassembler]"
    err=1
    return
  fi

  # check again with a target symbol name
  if ! perf annotate --no-demangle -i "${perfdata}" "${testsym}" 2> /dev/null | \
	  head -250 | grep -m 3 "${disasm_regex}"
  then
    echo "Basic annotate [Failed: missing disasm output when specifying the target symbol]"
    err=1
    return
  fi

  # check one more with external objdump tool (forced by --objdump option)
  if ! perf annotate --no-demangle -i "${perfdata}" --objdump=objdump 2> /dev/null | \
	  head -250 | grep -m 3 "${disasm_regex}"
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
