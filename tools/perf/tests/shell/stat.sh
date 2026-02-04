#!/bin/bash
# perf stat tests
# SPDX-License-Identifier: GPL-2.0

set -e

err=0
stat_output=$(mktemp /tmp/perf-stat-test-output.XXXXX)

cleanup() {
  rm -f "${stat_output}"
  trap - EXIT TERM INT
}

trap_cleanup() {
  echo "Unexpected signal in ${FUNCNAME[1]}"
  cleanup
  exit 1
}

trap trap_cleanup EXIT TERM INT

test_default_stat() {
  echo "Basic stat command test"
  if ! perf stat true 2>&1 | grep -E -q "Performance counter stats for 'true':"
  then
    echo "Basic stat command test [Failed]"
    err=1
    return
  fi
  echo "Basic stat command test [Success]"
}

test_null_stat() {
  echo "Null stat command test"
  if ! perf stat --null true 2>&1 | grep -E -q "Performance counter stats for 'true':"
  then
    echo "Null stat command test [Failed]"
    err=1
    return
  fi
  echo "Null stat command test [Success]"
}

find_offline_cpu() {
  for i in $(seq 1 4096)
  do
    if [[ ! -f /sys/devices/system/cpu/cpu$i/online || \
          $(cat /sys/devices/system/cpu/cpu$i/online) == "0" ]]
    then
      echo $i
      return
    fi
  done
  echo "Failed to find offline CPU"
  exit 1
}

test_offline_cpu_stat() {
  cpu=$(find_offline_cpu)
  echo "Offline CPU stat command test (cpu $cpu)"
  if ! perf stat "-C$cpu" -e cycles true 2>&1 | grep -E -q "No supported events found."
  then
    echo "Offline CPU stat command test [Failed]"
    err=1
    return
  fi
  echo "Offline CPU stat command test [Success]"
}

test_stat_record_report() {
  echo "stat record and report test"
  if ! perf stat record -e task-clock -o - true | perf stat report -i - 2>&1 | \
    grep -E -q "Performance counter stats for 'pipe':"
  then
    echo "stat record and report test [Failed]"
    err=1
    return
  fi
  echo "stat record and report test [Success]"
}

test_stat_record_script() {
  echo "stat record and script test"
  if ! perf stat record -e task-clock -o - true | perf script -i - 2>&1 | \
    grep -E -q "CPU[[:space:]]+THREAD[[:space:]]+VAL[[:space:]]+ENA[[:space:]]+RUN[[:space:]]+TIME[[:space:]]+EVENT"
  then
    echo "stat record and script test [Failed]"
    err=1
    return
  fi
  echo "stat record and script test [Success]"
}

test_stat_repeat_weak_groups() {
  echo "stat repeat weak groups test"
  if ! perf stat -e '{cycles,cycles,cycles,cycles,cycles,cycles,cycles,cycles,cycles,cycles}' \
     true 2>&1 | grep -q 'seconds time elapsed'
  then
    echo "stat repeat weak groups test [Skipped event parsing failed]"
    return
  fi
  if ! perf stat -r2 -e '{cycles,cycles,cycles,cycles,cycles,cycles,cycles,cycles,cycles,cycles}:W' \
    true > /dev/null 2>&1
  then
    echo "stat repeat weak groups test [Failed]"
    err=1
    return
  fi
  echo "stat repeat weak groups test [Success]"
}

test_topdown_groups() {
  # Topdown events must be grouped with the slots event first. Test that
  # parse-events reorders this.
  echo "Topdown event group test"
  if ! perf stat -e '{slots,topdown-retiring}' true > /dev/null 2>&1
  then
    echo "Topdown event group test [Skipped event parsing failed]"
    return
  fi
  td_err=0
  do_topdown_group_test() {
    events=$1
    failure=$2
    if perf stat -e "$events" true 2>&1 | grep -E -q "<not supported>"
    then
      echo "Topdown event group test [Failed $failure for '$events']"
      td_err=1
      return
    fi
  }
  do_topdown_group_test "{slots,topdown-retiring}" "events not supported"
  do_topdown_group_test "{instructions,r400,r8000}" "raw format slots not reordered first"
  filler_events=("instructions" "cycles"
                 "context-switches" "faults")
  for ((i = 0; i < ${#filler_events[@]}; i+=2))
  do
    filler1=${filler_events[i]}
    filler2=${filler_events[i+1]}
    do_topdown_group_test "$filler1,topdown-retiring,slots" \
      "slots not reordered first in no-group case"
    do_topdown_group_test "slots,$filler1,topdown-retiring" \
      "topdown metrics event not reordered in no-group case"
    do_topdown_group_test "{$filler1,topdown-retiring,slots}" \
      "slots not reordered first in single group case"
    do_topdown_group_test "{$filler1,slots},topdown-retiring" \
      "topdown metrics event not move into slots group"
    do_topdown_group_test "topdown-retiring,{$filler1,slots}" \
      "topdown metrics event not move into slots group last"
    do_topdown_group_test "{$filler1,slots},{topdown-retiring}" \
      "topdown metrics group not merge into slots group"
    do_topdown_group_test "{topdown-retiring},{$filler1,slots}" \
      "topdown metrics group not merge into slots group last"
    do_topdown_group_test "{$filler1,slots},$filler2,topdown-retiring" \
      "non-adjacent topdown metrics group not move into slots group"
    do_topdown_group_test "$filler2,topdown-retiring,{$filler1,slots}" \
      "non-adjacent topdown metrics group not move into slots group last"
    do_topdown_group_test "{$filler1,slots},{$filler2,topdown-retiring}" \
      "metrics group not merge into slots group"
    do_topdown_group_test "{$filler1,topdown-retiring},{$filler2,slots}" \
      "metrics group not merge into slots group last"
  done
  if test "$td_err" -eq 0
  then
    echo "Topdown event group test [Success]"
  else
    err="$td_err"
  fi
}

test_topdown_weak_groups() {
  # Weak groups break if the perf_event_open of multiple grouped events
  # fails. Breaking a topdown group causes the events to fail. Test a very large
  # grouping to see that the topdown events aren't broken out.
  echo "Topdown weak groups test"
  ok_grouping="{slots,topdown-bad-spec,topdown-be-bound,topdown-fe-bound,topdown-retiring},branch-instructions,branch-misses,bus-cycles,cache-misses,cache-references,cpu-cycles,instructions,mem-loads,mem-stores,ref-cycles,cache-misses,cache-references"
  if ! perf stat --no-merge -e "$ok_grouping" true > /dev/null 2>&1
  then
    echo "Topdown weak groups test [Skipped event parsing failed]"
    return
  fi
  group_needs_break="{slots,topdown-bad-spec,topdown-be-bound,topdown-fe-bound,topdown-retiring,branch-instructions,branch-misses,bus-cycles,cache-misses,cache-references,cpu-cycles,instructions,mem-loads,mem-stores,ref-cycles,cache-misses,cache-references}:W"
  if perf stat --no-merge -e "$group_needs_break" true 2>&1 | grep -E -q "<not supported>"
  then
    echo "Topdown weak groups test [Failed events not supported]"
    err=1
    return
  fi
  echo "Topdown weak groups test [Success]"
}

test_cputype() {
  # Test --cputype argument.
  echo "cputype test"

  # Bogus PMU should fail.
  if perf stat --cputype="123" -e instructions true > /dev/null 2>&1
  then
    echo "cputype test [Bogus PMU didn't fail]"
    err=1
    return
  fi

  # Find a known PMU for cputype.
  pmu=""
  devs="/sys/bus/event_source/devices"
  for i in $devs/cpu $devs/cpu_atom $devs/armv8_pmuv3_0 $devs/armv8_cortex_*
  do
    i_base=$(basename "$i")
    if test -d "$i"
    then
      pmu="$i_base"
      break
    fi
    if perf stat -e "$i_base/instructions/" true > /dev/null 2>&1
    then
      pmu="$i_base"
      break
    fi
  done
  if test "x$pmu" = "x"
  then
    echo "cputype test [Skipped known PMU not found]"
    return
  fi

  # Test running with cputype produces output.
  if ! perf stat --cputype="$pmu" -e instructions true 2>&1 | grep -E -q "instructions"
  then
    echo "cputype test [Failed count missed with given filter]"
    err=1
    return
  fi
  echo "cputype test [Success]"
}

test_hybrid() {
  # Test the default stat command on hybrid devices opens one cycles event for
  # each CPU type.
  echo "hybrid test"

  # Count the number of core PMUs, assume minimum of 1
  pmus=$(ls /sys/bus/event_source/devices/*/cpus 2>/dev/null | wc -l)
  if [ "$pmus" -lt 1 ]
  then
    pmus=1
  fi

  # Run default Perf stat
  cycles_events=$(perf stat -a -- sleep 0.1 2>&1 | grep -E "/cpu-cycles/[uH]*|  cpu-cycles[:uH]*  "  | wc -l)

  # The expectation is that default output will have a cycles events on each
  # hybrid PMU. In situations with no cycles PMU events, like virtualized, this
  # can fall back to task-clock and so the end count may be 0. Fail if neither
  # condition holds.
  if [ "$pmus" -ne "$cycles_events" ] && [ "0" -ne "$cycles_events" ]
  then
    echo "hybrid test [Found $pmus PMUs but $cycles_events cycles events. Failed]"
    err=1
    return
  fi
  echo "hybrid test [Success]"
}

test_stat_cpu() {
  echo "stat -C <cpu> test"
  # Test the full online CPU list (ranges and lists)
  online_cpus=$(cat /sys/devices/system/cpu/online)
  if ! perf stat -C "$online_cpus" -a true > "${stat_output}" 2>&1
  then
    echo "stat -C <cpu> test [Failed - command failed for cpus $online_cpus]"
    cat "${stat_output}"
    err=1
    return
  fi

  if ! grep -E -q "Performance counter stats for" "${stat_output}"
  then
    echo "stat -C <cpu> test [Failed - missing output for cpus $online_cpus]"
    cat "${stat_output}"
    err=1
    return
  fi

  # Test each individual online CPU
  for cpu_dir in /sys/devices/system/cpu/cpu[0-9]*; do
    cpu=${cpu_dir##*/cpu}
    # Check if online
    if [ -f "$cpu_dir/online" ] && [ "$(cat "$cpu_dir/online")" -eq 0 ]
    then
      continue
    fi

    if ! perf stat -C "$cpu" -a true > "${stat_output}" 2>&1
    then
      echo "stat -C <cpu> test [Failed - command failed for cpu $cpu]"
      cat "${stat_output}"
      err=1
      return
    fi
    if ! grep -E -q "Performance counter stats for" "${stat_output}"
    then
      echo "stat -C <cpu> test [Failed - missing output for cpu $cpu]"
      cat "${stat_output}"
      err=1
      return
    fi
  done

  # Test synthetic list and range if cpu0 and cpu1 are online
  c0_online=0
  c1_online=0
  if [ -d "/sys/devices/system/cpu/cpu0" ]
  then
    if [ ! -f "/sys/devices/system/cpu/cpu0/online" ] || [ "$(cat /sys/devices/system/cpu/cpu0/online)" -eq 1 ]
    then
      c0_online=1
    fi
  fi
  if [ -d "/sys/devices/system/cpu/cpu1" ]
  then
    if [ ! -f "/sys/devices/system/cpu/cpu1/online" ] || [ "$(cat /sys/devices/system/cpu/cpu1/online)" -eq 1 ]
    then
      c1_online=1
    fi
  fi

  if [ $c0_online -eq 1 ] && [ $c1_online -eq 1 ]
  then
    # Test list "0,1"
    if ! perf stat -C "0,1" -a true > "${stat_output}" 2>&1
    then
      echo "stat -C <cpu> test [Failed - command failed for cpus 0,1]"
      cat "${stat_output}"
      err=1
      return
    fi
    if ! grep -E -q "Performance counter stats for" "${stat_output}"
    then
      echo "stat -C <cpu> test [Failed - missing output for cpus 0,1]"
      cat "${stat_output}"
      err=1
      return
    fi

    # Test range "0-1"
    if ! perf stat -C "0-1" -a true > "${stat_output}" 2>&1
    then
      echo "stat -C <cpu> test [Failed - command failed for cpus 0-1]"
      cat "${stat_output}"
      err=1
      return
    fi
    if ! grep -E -q "Performance counter stats for" "${stat_output}"
    then
      echo "stat -C <cpu> test [Failed - missing output for cpus 0-1]"
      cat "${stat_output}"
      err=1
      return
    fi
  fi

  echo "stat -C <cpu> test [Success]"
}

test_stat_no_aggr() {
  echo "stat -A test"
  if ! perf stat -A -a true > "${stat_output}" 2>&1
  then
    echo "stat -A test [Failed - command failed]"
    cat "${stat_output}"
    err=1
    return
  fi

  if ! grep -E -q "CPU" "${stat_output}"
  then
    echo "stat -A test [Failed - missing CPU column]"
    cat "${stat_output}"
    err=1
    return
  fi
  echo "stat -A test [Success]"
}

test_stat_detailed() {
  echo "stat -d test"
  if ! perf stat -d true > "${stat_output}" 2>&1
  then
    echo "stat -d test [Failed - command failed]"
    cat "${stat_output}"
    err=1
    return
  fi

  if ! grep -E -q "Performance counter stats" "${stat_output}"
  then
    echo "stat -d test [Failed - missing output]"
    cat "${stat_output}"
    err=1
    return
  fi

  if ! perf stat -dd true > "${stat_output}" 2>&1
  then
    echo "stat -dd test [Failed - command failed]"
    cat "${stat_output}"
    err=1
    return
  fi

  if ! grep -E -q "Performance counter stats" "${stat_output}"
  then
    echo "stat -dd test [Failed - missing output]"
    cat "${stat_output}"
    err=1
    return
  fi

  if ! perf stat -ddd true > "${stat_output}" 2>&1
  then
    echo "stat -ddd test [Failed - command failed]"
    cat "${stat_output}"
    err=1
    return
  fi

  if ! grep -E -q "Performance counter stats" "${stat_output}"
  then
    echo "stat -ddd test [Failed - missing output]"
    cat "${stat_output}"
    err=1
    return
  fi

  echo "stat -d test [Success]"
}

test_stat_repeat() {
  echo "stat -r test"
  if ! perf stat -r 2 true > "${stat_output}" 2>&1
  then
    echo "stat -r test [Failed - command failed]"
    cat "${stat_output}"
    err=1
    return
  fi

  if ! grep -E -q "\([[:space:]]*\+-.*%[[:space:]]*\)" "${stat_output}"
  then
    echo "stat -r test [Failed - missing variance]"
    cat "${stat_output}"
    err=1
    return
  fi
  echo "stat -r test [Success]"
}

test_stat_pid() {
  echo "stat -p test"
  sleep 1 &
  pid=$!
  if ! perf stat -p $pid > "${stat_output}" 2>&1
  then
    echo "stat -p test [Failed - command failed]"
    cat "${stat_output}"
    err=1
    kill $pid 2>/dev/null || true
    wait $pid 2>/dev/null || true
    return
  fi

  if ! grep -E -q "Performance counter stats" "${stat_output}"
  then
    echo "stat -p test [Failed - missing output]"
    cat "${stat_output}"
    err=1
  else
    echo "stat -p test [Success]"
  fi
  kill $pid 2>/dev/null || true
  wait $pid 2>/dev/null || true
}

test_default_stat
test_null_stat
test_offline_cpu_stat
test_stat_record_report
test_stat_record_script
test_stat_repeat_weak_groups
test_topdown_groups
test_topdown_weak_groups
test_cputype
test_hybrid
test_stat_cpu
test_stat_no_aggr
test_stat_detailed
test_stat_repeat
test_stat_pid

cleanup
exit $err
