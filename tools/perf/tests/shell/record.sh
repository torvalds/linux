#!/bin/sh
# perf record tests
# SPDX-License-Identifier: GPL-2.0

set -e

err=0
test_per_thread() {
  echo "Basic --per-thread mode test"
  perf record -e instructions:u --per-thread -o- true 2> /dev/null \
    | perf report -i- -q \
    | egrep -q true
  echo "Basic --per-thread mode test [Success]"
}

test_register_capture() {
  echo "Register capture test"
  if ! perf list | egrep -q 'br_inst_retired.near_call'
  then
    echo "Register capture test [Skipped missing instruction]"
    return
  fi
  if ! perf record --intr-regs=\? 2>&1 | egrep -q 'available registers: AX BX CX DX SI DI BP SP IP FLAGS CS SS R8 R9 R10 R11 R12 R13 R14 R15'
  then
    echo "Register capture test [Skipped missing registers]"
    return
  fi
  if ! perf record -o - --intr-regs=di,r8,dx,cx -e cpu/br_inst_retired.near_call/p \
    -c 1000 --per-thread true 2> /dev/null \
    | perf script -F ip,sym,iregs -i - 2> /dev/null \
    | egrep -q "DI:"
  then
    echo "Register capture test [Failed missing output]"
    err=1
    return
  fi
  echo "Register capture test [Success]"
}

test_per_thread
test_register_capture
exit $err
