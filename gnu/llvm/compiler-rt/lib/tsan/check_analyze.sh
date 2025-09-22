#!/usr/bin/env bash
#
# Script that checks that critical functions in TSan runtime have correct number
# of push/pop/rsp instructions to verify that runtime is efficient enough.
#
# This test can fail when backend code generation changes the output for various
# tsan interceptors. When such a change happens, you can ensure that the
# performance has not regressed by running the following benchmarks before and
# after the breaking change to verify that the values in this file are safe to
# update:
# ./projects/compiler-rt/lib/tsan/tests/rtl/TsanRtlTest-x86_64-Test
#   --gtest_also_run_disabled_tests --gtest_filter=DISABLED_BENCH.Mop*

set -u

if [[ "$#" != 1 ]]; then
  echo "Usage: $0 /path/to/binary/built/with/tsan"
  exit 1
fi

SCRIPTDIR=$(dirname $0)
RES=$(${SCRIPTDIR}/analyze_libtsan.sh $1)
PrintRes() {
  printf "%s\n" "$RES"
}

PrintRes

check() {
  res=$(PrintRes | egrep "$1 .* $2 $3; ")
  if [ "$res" == "" ]; then
    echo FAILED $1 must contain $2 $3
    exit 1
  fi
}

# All hot functions must contain no PUSH/POP
# and no CALLs (everything is tail-called).
for f in write1 write2 write4 write8; do
  check $f rsp 1
  check $f push 0
  check $f pop 0
  check $f call 0
done

for f in read1 read2 read4 read8; do
  check $f rsp 1
  check $f push 0
  check $f pop 0
  check $f call 0
done

for f in func_entry func_exit; do
  check $f rsp 0
  check $f push 0
  check $f pop 0
  check $f call 0
done

echo LGTM
