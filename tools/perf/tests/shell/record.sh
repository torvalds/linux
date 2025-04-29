#!/bin/bash
# perf record tests (exclusive)
# SPDX-License-Identifier: GPL-2.0

set -e

shelldir=$(dirname "$0")
# shellcheck source=lib/waiting.sh
. "${shelldir}"/lib/waiting.sh

# shellcheck source=lib/perf_has_symbol.sh
. "${shelldir}"/lib/perf_has_symbol.sh

testsym="test_loop"

skip_test_missing_symbol ${testsym}

err=0
perfdata=$(mktemp /tmp/__perf_test.perf.data.XXXXX)
script_output=$(mktemp /tmp/__perf_test.perf.data.XXXXX.script)
testprog="perf test -w thloop"
cpu_pmu_dir="/sys/bus/event_source/devices/cpu*"
br_cntr_file="/caps/branch_counter_nr"
br_cntr_output="branch stack counters"
br_cntr_script_output="br_cntr: A"

default_fd_limit=$(ulimit -Sn)
# With option --threads=cpu the number of open file descriptors should be
# equal to sum of:    nmb_cpus * nmb_events (2+dummy),
#                     nmb_threads for perf.data.n (equal to nmb_cpus) and
#                     2*nmb_cpus of pipes = 4*nmb_cpus (each pipe has 2 ends)
# All together it needs 8*nmb_cpus file descriptors plus some are also used
# outside of testing, thus raising the limit to 16*nmb_cpus
min_fd_limit=$(($(getconf _NPROCESSORS_ONLN) * 16))

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

test_per_thread() {
  echo "Basic --per-thread mode test"
  if ! perf record -o /dev/null --quiet ${testprog} 2> /dev/null
  then
    echo "Per-thread record [Skipped event not supported]"
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

  # run the test program in background (for 30 seconds)
  ${testprog} 30 &
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
  if ! perf list pmu | grep -q 'br_inst_retired.near_call'
  then
    echo "Register capture test [Skipped missing event]"
    return
  fi
  if ! perf record --intr-regs=\? 2>&1 | grep -q 'available registers: AX BX CX DX SI DI BP SP IP FLAGS CS SS R8 R9 R10 R11 R12 R13 R14 R15'
  then
    echo "Register capture test [Skipped missing registers]"
    return
  fi
  if ! perf record -o - --intr-regs=di,r8,dx,cx -e br_inst_retired.near_call \
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

test_system_wide() {
  echo "Basic --system-wide mode test"
  if ! perf record -aB --synth=no -o "${perfdata}" ${testprog} 2> /dev/null
  then
    echo "System-wide record [Skipped not supported]"
    return
  fi
  if ! perf report -i "${perfdata}" -q | grep -q "${testsym}"
  then
    echo "System-wide record [Failed missing output]"
    err=1
    return
  fi
  if ! perf record -aB --synth=no -e cpu-clock,cs --threads=cpu \
    -o "${perfdata}" ${testprog} 2> /dev/null
  then
    echo "System-wide record [Failed record --threads option]"
    err=1
    return
  fi
  if ! perf report -i "${perfdata}" -q | grep -q "${testsym}"
  then
    echo "System-wide record [Failed --threads missing output]"
    err=1
    return
  fi
  echo "Basic --system-wide mode test [Success]"
}

test_workload() {
  echo "Basic target workload test"
  if ! perf record -o "${perfdata}" ${testprog} 2> /dev/null
  then
    echo "Workload record [Failed record]"
    err=1
    return
  fi
  if ! perf report -i "${perfdata}" -q | grep -q "${testsym}"
  then
    echo "Workload record [Failed missing output]"
    err=1
    return
  fi
  if ! perf record -e cpu-clock,cs --threads=package \
    -o "${perfdata}" ${testprog} 2> /dev/null
  then
    echo "Workload record [Failed record --threads option]"
    err=1
    return
  fi
  if ! perf report -i "${perfdata}" -q | grep -q "${testsym}"
  then
    echo "Workload record [Failed --threads missing output]"
    err=1
    return
  fi
  echo "Basic target workload test [Success]"
}

test_branch_counter() {
  echo "Branch counter test"
  # Check if the branch counter feature is supported
  for dir in $cpu_pmu_dir
  do
    if [ ! -e "$dir$br_cntr_file" ]
    then
      echo "branch counter feature not supported on all core PMUs ($dir) [Skipped]"
      return
    fi
  done
  if ! perf record -o "${perfdata}" -e "{branches:p,instructions}" -j any,counter ${testprog} 2> /dev/null
  then
    echo "Branch counter record test [Failed record]"
    err=1
    return
  fi
  if ! perf report -i "${perfdata}" -D -q | grep -q "$br_cntr_output"
  then
    echo "Branch counter report test [Failed missing output]"
    err=1
    return
  fi
  if ! perf script -i "${perfdata}" -F +brstackinsn,+brcntr | grep -q "$br_cntr_script_output"
  then
    echo " Branch counter script test [Failed missing output]"
    err=1
    return
  fi
  echo "Branch counter test [Success]"
}

test_cgroup() {
  echo "Cgroup sampling test"
  if ! perf record -aB --synth=cgroup --all-cgroups -o "${perfdata}" ${testprog} 2> /dev/null
  then
    echo "Cgroup sampling [Skipped not supported]"
    return
  fi
  if ! perf report -i "${perfdata}" -D | grep -q "CGROUP"
  then
    echo "Cgroup sampling [Failed missing output]"
    err=1
    return
  fi
  if ! perf script -i "${perfdata}" -F cgroup | grep -q -v "unknown"
  then
    echo "Cgroup sampling [Failed cannot resolve cgroup names]"
    err=1
    return
  fi
  echo "Cgroup sampling test [Success]"
}

test_leader_sampling() {
  echo "Basic leader sampling test"
  if ! perf record -o "${perfdata}" -e "{cycles,cycles}:Su" -- \
    perf test -w brstack 2> /dev/null
  then
    echo "Leader sampling [Failed record]"
    err=1
    return
  fi
  index=0
  perf script -i "${perfdata}" > $script_output
  while IFS= read -r line
  do
    # Check if the two instruction counts are equal in each record
    cycles=$(echo $line | awk '{for(i=1;i<=NF;i++) if($i=="cycles:") print $(i-1)}')
    if [ $(($index%2)) -ne 0 ] && [ ${cycles}x != ${prev_cycles}x ]
    then
      echo "Leader sampling [Failed inconsistent cycles count]"
      err=1
      return
    fi
    index=$(($index+1))
    prev_cycles=$cycles
  done < $script_output
  echo "Basic leader sampling test [Success]"
}

test_topdown_leader_sampling() {
  echo "Topdown leader sampling test"
  if ! perf stat -e "{slots,topdown-retiring}" true 2> /dev/null
  then
    echo "Topdown leader sampling [Skipped event parsing failed]"
    return
  fi
  if ! perf record -o "${perfdata}" -e "{instructions,slots,topdown-retiring}:S" true 2> /dev/null
  then
    echo "Topdown leader sampling [Failed topdown events not reordered correctly]"
    err=1
    return
  fi
  echo "Topdown leader sampling test [Success]"
}

test_precise_max() {
  local -i skipped=0

  echo "precise_max attribute test"
  # Just to make sure event cycles is supported for sampling
  if perf record -o "${perfdata}" -e "cycles" true 2> /dev/null
  then
    if ! perf record -o "${perfdata}" -e "cycles:P" true 2> /dev/null
    then
      echo "precise_max attribute [Failed cycles:P event]"
      err=1
      return
    fi
  else
    echo "precise_max attribute [Skipped no cycles:P event]"
    ((skipped+=1))
  fi
  # On s390 event instructions is not supported for perf record
  if perf record -o "${perfdata}" -e "instructions" true 2> /dev/null
  then
    # On AMD, cycles and instructions events are treated differently
    if ! perf record -o "${perfdata}" -e "instructions:P" true 2> /dev/null
    then
      echo "precise_max attribute [Failed instructions:P event]"
      err=1
      return
    fi
  else
    echo "precise_max attribute [Skipped no instructions:P event]"
    ((skipped+=1))
  fi
  if [ $skipped -eq 2 ]
  then
    echo "precise_max attribute [Skipped no hardware events]"
  else
    echo "precise_max attribute test [Success]"
  fi
}

# raise the limit of file descriptors to minimum
if [[ $default_fd_limit -lt $min_fd_limit ]]; then
       ulimit -Sn $min_fd_limit
fi

test_per_thread
test_register_capture
test_system_wide
test_workload
test_branch_counter
test_cgroup
test_leader_sampling
test_topdown_leader_sampling
test_precise_max

# restore the default value
ulimit -Sn $default_fd_limit

cleanup
exit $err
