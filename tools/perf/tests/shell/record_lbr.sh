#!/bin/bash
# perf record LBR tests (exclusive)
# SPDX-License-Identifier: GPL-2.0

set -e

ParanoidAndNotRoot() {
  [ "$(id -u)" != 0 ] && [ "$(cat /proc/sys/kernel/perf_event_paranoid)" -gt $1 ]
}

if [ ! -f /sys/bus/event_source/devices/cpu/caps/branches ] &&
   [ ! -f /sys/bus/event_source/devices/cpu_core/caps/branches ]
then
  echo "Skip: only x86 CPUs support LBR"
  exit 2
fi

err=0
perfdata=$(mktemp /tmp/__perf_test.perf.data.XXXXX)

cleanup() {
  rm -rf "${perfdata}"
  rm -rf "${perfdata}".old
  rm -rf "${perfdata}".txt

  trap - EXIT TERM INT
}

trap_cleanup() {
  echo "Unexpected signal in ${FUNCNAME[1]}"
  cleanup
  exit 1
}
trap trap_cleanup EXIT TERM INT


lbr_callgraph_test() {
  test="LBR callgraph"

  echo "$test"
  if ! perf record -e cycles --call-graph lbr -o "${perfdata}" perf test -w thloop
  then
    echo "$test [Failed support missing]"
    if [ $err -eq 0 ]
    then
      err=2
    fi
    return
  fi

  if ! perf report --stitch-lbr -i "${perfdata}" > "${perfdata}".txt
  then
    cat "${perfdata}".txt
    echo "$test [Failed in perf report]"
    err=1
    return
  fi

  echo "$test [Success]"
}

lbr_test() {
  local branch_flags=$1
  local test="LBR $2 test"
  local threshold=$3
  local out
  local sam_nr
  local bs_nr
  local zero_nr
  local r

  echo "$test"
  if ! perf record -e cycles $branch_flags -o "${perfdata}" perf test -w thloop
  then
    echo "$test [Failed support missing]"
    perf record -e cycles $branch_flags -o "${perfdata}" perf test -w thloop || true
    if [ $err -eq 0 ]
    then
      err=2
    fi
    return
  fi

  out=$(perf report -D -i "${perfdata}" 2> /dev/null | grep -A1 'PERF_RECORD_SAMPLE')
  sam_nr=$(echo "$out" | grep -c 'PERF_RECORD_SAMPLE' || true)
  if [ $sam_nr -eq 0 ]
  then
    echo "$test [Failed no samples captured]"
    err=1
    return
  fi
  echo "$test: $sam_nr samples"

  bs_nr=$(echo "$out" | grep -c 'branch stack: nr:' || true)
  if [ $sam_nr -ne $bs_nr ]
  then
    echo "$test [Failed samples missing branch stacks]"
    err=1
    return
  fi

  zero_nr=$(echo "$out" | grep -A3 'branch stack: nr:0' | grep thread | grep -cv swapper || true)
  r=$(($zero_nr * 100 / $bs_nr))
  if [ $r -gt $threshold ]; then
    echo "$test [Failed empty br stack ratio exceed $threshold%: $r%]"
    err=1
    return
  fi

  echo "$test [Success]"
}

parallel_lbr_test() {
  err=0
  perfdata=$(mktemp /tmp/__perf_test.perf.data.XXXXX)
  lbr_test "$1" "$2" "$3"
  cleanup
  exit $err
}

lbr_callgraph_test

# Sequential
lbr_test "-b" "any branch" 2
lbr_test "-j any_call" "any call" 2
lbr_test "-j any_ret" "any ret" 2
lbr_test "-j ind_call" "any indirect call" 2
lbr_test "-j ind_jmp" "any indirect jump" 100
lbr_test "-j call" "direct calls" 2
lbr_test "-j ind_call,u" "any indirect user call" 100
if ! ParanoidAndNotRoot 1
then
  lbr_test "-a -b" "system wide any branch" 2
  lbr_test "-a -j any_call" "system wide any call" 2
fi

# Parallel
parallel_lbr_test "-b" "parallel any branch" 100 &
pid1=$!
parallel_lbr_test "-j any_call" "parallel any call" 100 &
pid2=$!
parallel_lbr_test "-j any_ret" "parallel any ret" 100 &
pid3=$!
parallel_lbr_test "-j ind_call" "parallel any indirect call" 100 &
pid4=$!
parallel_lbr_test "-j ind_jmp" "parallel any indirect jump" 100 &
pid5=$!
parallel_lbr_test "-j call" "parallel direct calls" 100 &
pid6=$!
parallel_lbr_test "-j ind_call,u" "parallel any indirect user call" 100 &
pid7=$!
if ParanoidAndNotRoot 1
then
  pid8=
  pid9=
else
  parallel_lbr_test "-a -b" "parallel system wide any branch" 100 &
  pid8=$!
  parallel_lbr_test "-a -j any_call" "parallel system wide any call" 100 &
  pid9=$!
fi

for pid in $pid1 $pid2 $pid3 $pid4 $pid5 $pid6 $pid7 $pid8 $pid9
do
  set +e
  wait $pid
  child_err=$?
  set -e
  if ([ $err -eq 2 ] && [ $child_err -eq 1 ]) || [ $err -eq 0 ]
  then
    err=$child_err
  fi
done

cleanup
exit $err
