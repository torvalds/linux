#!/bin/sh
# perf record tests
# SPDX-License-Identifier: GPL-2.0

set -e

shelldir=$(dirname "$0")
. "${shelldir}"/lib/waiting.sh

err=0
perfdata=$(mktemp /tmp/__perf_test.perf.data.XXXXX)
testprog=$(mktemp /tmp/__perf_test.prog.XXXXXX)
testsym="test_loop"

cleanup() {
  rm -f "${perfdata}"
  rm -f "${perfdata}".old

  if [ "${testprog}" != "true" ]; then
    rm -f "${testprog}"
  fi

  trap - EXIT TERM INT
}

trap_cleanup() {
  cleanup
  exit 1
}
trap trap_cleanup EXIT TERM INT

build_test_program() {
  if ! [ -x "$(command -v cc)" ]; then
    # No CC found. Fall back to 'true'
    testprog=true
    testsym=true
    return
  fi

  echo "Build a test program"
  cat <<EOF | cc -o ${testprog} -xc - -pthread
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

void test_loop(void) {
  volatile int count = 1000000;

  while (count--)
    continue;
}

void *thfunc(void *arg) {
  int forever = *(int *)arg;

  do {
    test_loop();
  } while (forever);

  return NULL;
}

int main(int argc, char *argv[]) {
  pthread_t th;
  int forever = 0;

  if (argc > 1)
    forever = atoi(argv[1]);

  pthread_create(&th, NULL, thfunc, &forever);
  test_loop();
  pthread_join(th, NULL);

  return 0;
}
EOF
}

test_per_thread() {
  echo "Basic --per-thread mode test"
  if ! perf record -o /dev/null --quiet ${testprog} 2> /dev/null
  then
    echo "Per-thread record [Skipped event not supported]"
    if [ $err -ne 1 ]
    then
      err=2
    fi
    return
  fi
  if ! perf record --per-thread -o "${perfdata}" ${testprog} 2> /dev/null
  then
    echo "Per-thread record [Failed record]"
    err=1
    return
  fi
  if ! perf report -i "${perfdata}" -q | grep -q "${testsym}"
  then
    echo "Per-thread record [Failed missing output]"
    err=1
    return
  fi

  # run the test program in background (forever)
  ${testprog} 1 &
  TESTPID=$!

  rm -f "${perfdata}"

  wait_for_threads ${TESTPID} 2
  perf record -p "${TESTPID}" --per-thread -o "${perfdata}" sleep 1 2> /dev/null
  kill ${TESTPID}

  if [ ! -e "${perfdata}" ]
  then
    echo "Per-thread record [Failed record -p]"
    err=1
    return
  fi
  if ! perf report -i "${perfdata}" -q | grep -q "${testsym}"
  then
    echo "Per-thread record [Failed -p missing output]"
    err=1
    return
  fi

  echo "Basic --per-thread mode test [Success]"
}

test_register_capture() {
  echo "Register capture test"
  if ! perf list | grep -q 'br_inst_retired.near_call'
  then
    echo "Register capture test [Skipped missing event]"
    if [ $err -ne 1 ]
    then
      err=2
    fi
    return
  fi
  if ! perf record --intr-regs=\? 2>&1 | grep -q 'available registers: AX BX CX DX SI DI BP SP IP FLAGS CS SS R8 R9 R10 R11 R12 R13 R14 R15'
  then
    echo "Register capture test [Skipped missing registers]"
    return
  fi
  if ! perf record -o - --intr-regs=di,r8,dx,cx -e br_inst_retired.near_call:p \
    -c 1000 --per-thread ${testprog} 2> /dev/null \
    | perf script -F ip,sym,iregs -i - 2> /dev/null \
    | grep -q "DI:"
  then
    echo "Register capture test [Failed missing output]"
    err=1
    return
  fi
  echo "Register capture test [Success]"
}

build_test_program

test_per_thread
test_register_capture

cleanup
exit $err
