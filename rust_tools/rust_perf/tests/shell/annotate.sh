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
  mode=$1
  echo "${mode} perf annotate test"
  if [ "x${mode}" == "xBasic" ]
  then
    perf record -o "${perfdata}" ${testprog} 2> /dev/null
  else
    perf record -o - ${testprog} 2> /dev/null > "${perfdata}"
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
    perf annotate --no-demangle -i "${perfdata}" --stdio --percent-limit 10 2> /dev/null > "${perfout}"
  else
    perf annotate --no-demangle -i - --stdio 2> /dev/null --percent-limit 10 < "${perfdata}" > "${perfout}"
  fi

  # check if it has the target symbol
  if ! grep -q "${testsym}" "${perfout}"
  then
    echo "${mode} annotate [Failed: missing target symbol]"
    cat "${perfout}"
    err=1
    return
  fi

  # check if it has the disassembly lines
  if ! grep -q "${disasm_regex}" "${perfout}"
  then
    echo "${mode} annotate [Failed: missing disasm output from default disassembler]"
    err=1
    return
  fi

  # check again with a target symbol name
  if [ "x${mode}" == "xBasic" ]
  then
    perf annotate --no-demangle -i "${perfdata}" "${testsym}" 2> /dev/null > "${perfout}"
  else
    perf annotate --no-demangle -i - "${testsym}" 2> /dev/null < "${perfdata}" > "${perfout}"
  fi

  if ! head -250 "${perfout}"| grep -q -m 3 "${disasm_regex}"
  then
    echo "${mode} annotate [Failed: missing disasm output when specifying the target symbol]"
    err=1
    return
  fi

  # check one more with external objdump tool (forced by --objdump option)
  if [ "x${mode}" == "xBasic" ]
  then
    perf annotate --no-demangle -i "${perfdata}" --percent-limit 10 --objdump=objdump 2> /dev/null > "${perfout}"
  else
    perf annotate --no-demangle -i - "${testsym}" --percent-limit 10 --objdump=objdump 2> /dev/null < "${perfdata}" > "${perfout}"
  fi
  if ! grep -q -m 3 "${disasm_regex}" "${perfout}"
  then
    echo "${mode} annotate [Failed: missing disasm output from non default disassembler (using --objdump)]"
    err=1
    return
  fi
  echo "${mode} annotate test [Success]"
}

test_basic Basic
test_basic Pipe

cleanup
exit $err
