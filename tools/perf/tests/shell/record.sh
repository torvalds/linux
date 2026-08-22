#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# perf record tests

set -e

shelldir=$(dirname "$0")
. "${shelldir}"/lib/perf_record.sh


# shellcheck source=lib/waiting.sh
. "${shelldir}"/lib/waiting.sh

# shellcheck source=lib/perf_has_symbol.sh
. "${shelldir}"/lib/perf_has_symbol.sh

testsym="test_loop"
testsym2="brstack"

skip_test_missing_symbol ${testsym}
skip_test_missing_symbol ${testsym2}

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
  rm -f "${perfdata}"
  rm -f "${perfdata}".old
  rm -f "${script_output}"
  perf_record_cleanup

  trap - EXIT TERM INT
}

trap_cleanup() {
  echo "Unexpected signal in ${FUNCNAME[1]}"
  cleanup
  exit 1
}
trap trap_cleanup EXIT TERM INT

check_per_thread() {
  perf report -i "${perfdata}" -q | grep -q "${testsym}"
}

test_per_thread() {
  echo "Basic --per-thread mode test"
  local ret=0
  perf_record_with_retry "${perfdata}" "check_per_thread" "perf test -w thloop" \
    --per-thread || ret=$?
  if [ $ret -eq 2 ]; then
    echo "Per-thread record [Skipped event not supported]"
    return
  elif [ $ret -eq 1 ]; then
    echo "Per-thread record [Failed record or missing output]"
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

check_register_capture() {
  perf script -F ip,sym,iregs -i "${perfdata}" 2>/dev/null | grep -q "DI:"
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

  local ret=0
  perf_record_with_retry "${perfdata}" "check_register_capture" "perf test -w thloop" \
    --intr-regs=di,r8,dx,cx -e br_inst_retired.near_call -c 1000 --per-thread || ret=$?

  if [ $ret -ne 0 ]; then
    echo "Register capture test [Failed missing output]"
    err=1
    return
  fi
  echo "Register capture test [Success]"
}

check_system_wide() {
  perf report -i "${perfdata}" -q | grep -q "${testsym}"
}

test_system_wide() {
  echo "Basic --system-wide mode test"
  local ret=0
  perf_record_with_retry "${perfdata}" "check_system_wide" "perf test -w thloop" \
    -aB --synth=no || ret=$?
  if [ $ret -eq 2 ]; then
    echo "System-wide record [Skipped not supported]"
    return
  elif [ $ret -eq 1 ]; then
    echo "System-wide record [Failed missing output]"
    err=1
    return
  fi

  ret=0
  perf_record_with_retry "${perfdata}" "check_system_wide" "perf test -w thloop" \
    -aB --synth=no -e cpu-clock,cs --threads=cpu || ret=$?
  if [ $ret -ne 0 ]; then
    echo "System-wide record [Failed record --threads option or missing output]"
    err=1
    return
  fi
  echo "Basic --system-wide mode test [Success]"
}

check_workload() {
  perf report -i "${perfdata}" -q | grep -q "${testsym}"
}

test_workload() {
  echo "Basic target workload test"
  local ret=0
  perf_record_with_retry "${perfdata}" "check_workload" "perf test -w thloop" || ret=$?
  if [ $ret -ne 0 ]; then
    echo "Workload record [Failed record or missing output]"
    err=1
    return
  fi

  ret=0
  perf_record_with_retry "${perfdata}" "check_workload" "perf test -w thloop" \
    -e cpu-clock,cs --threads=package || ret=$?
  if [ $ret -ne 0 ]; then
    echo "Workload record [Failed record --threads option or missing output]"
    err=1
    return
  fi
  echo "Basic target workload test [Success]"
}

check_branch_counter() {
  perf report -i "${perfdata}" -D -q 2>/dev/null | grep -q "$br_cntr_output" && \
  perf script -i "${perfdata}" -F +brstackinsn,+brcntr 2>/dev/null | \
    grep -q "$br_cntr_script_output"
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
  local ret=0
  perf_record_with_retry "${perfdata}" "check_branch_counter" "perf test -w thloop" \
    -e "{branches:p,instructions}" -j any,counter || ret=$?
  if [ $ret -ne 0 ]; then
    echo "Branch counter test [Failed record or missing output]"
    err=1
    return
  fi
  echo "Branch counter test [Success]"
}

check_cgroup() {
  perf report -i "${perfdata}" -D 2>/dev/null | grep -q "CGROUP" && \
  perf script -i "${perfdata}" -F cgroup 2>/dev/null | grep -q -v "unknown"
}

test_cgroup() {
  echo "Cgroup sampling test"
  local ret=0
  perf_record_with_retry "${perfdata}" "check_cgroup" "perf test -w thloop" \
    -aB --synth=cgroup --all-cgroups || ret=$?
  if [ $ret -eq 2 ]; then
    echo "Cgroup sampling [Skipped not supported]"
    return
  elif [ $ret -eq 1 ]; then
    echo "Cgroup sampling [Failed missing output]"
    err=1
    return
  fi
  echo "Cgroup sampling test [Success]"
}

check_uid() {
  perf report -i "${perfdata}" -q | grep -q "${testsym}"
}

test_uid() {
  echo "Uid sampling test"
  local ret=0
  perf_record_with_retry "${perfdata}" "check_uid" "perf test -w thloop" \
    -aB --synth=no --uid "$(id -u)" || ret=$?
  if [ $ret -eq 2 ]; then
    local logfile="${PERF_RECORD_LOGS[${#PERF_RECORD_LOGS[@]}-1]}"
    if grep -q -E "libbpf.*EPERM|Access to performance monitoring" "$logfile" || \
       grep -q -E "Permission denied|Failure to open any events" "$logfile"
    then
      echo "Uid sampling [Skipped permissions]"
      return
    else
      echo "Uid sampling [Failed to record]"
      err=1
      return
    fi
  elif [ $ret -eq 1 ]; then
    echo "Uid sampling [Failed missing output]"
    err=1
    return
  fi
  echo "Uid sampling test [Success]"
}

test_leader_sampling() {
  echo "Basic leader sampling test"
  events="{cycles,cycles}:Su"
  [ "$(uname -m)" = "s390x" ] && {
    [ ! -d /sys/devices/cpum_sf ] && {
      echo "No CPUMF [Skipped record]"
      return
    }
    events="{cpum_sf/SF_CYCLES_BASIC/,cycles}:Su"
    perf record -o "${perfdata}" -e "$events" -- perf test -w brstack 2> /dev/null
    # Perf grouping might be unsupported, depends on version.
    [ "$?" -ne 0 ] && {
      echo "Grouping not support [Skipped record]"
      return
    }
  }
  if ! perf record -o "${perfdata}" -e "$events" -- \
    perf test -w brstack 2> /dev/null
  then
    echo "Leader sampling [Failed record]"
    err=1
    return
  fi
  perf script -i "${perfdata}" | grep brstack > $script_output
  # Check if the two instruction counts are equal in each record.
  # However, the throttling code doesn't consider event grouping. During throttling, only the
  # leader is stopped, causing the slave's counts significantly higher. To temporarily solve this,
  # let's set the tolerance rate to 80%.
  # TODO: Revert the code for tolerance once the throttling mechanism is fixed.
  index=0
  valid_counts=0
  invalid_counts=0
  tolerance_rate=0.8
  while IFS= read -r line
  do
    cycles=$(echo $line | awk '{for(i=1;i<=NF;i++) if($i=="cycles:") print $(i-1)}')
    if [ $(($index%2)) -ne 0 ] && [ ${cycles}x != ${prev_cycles}x ]
    then
      invalid_counts=$(($invalid_counts+1))
    else
      valid_counts=$(($valid_counts+1))
    fi
    index=$(($index+1))
    prev_cycles=$cycles
  done < "${script_output}"
  total_counts=$(bc <<< "$invalid_counts+$valid_counts")
  if (( $(bc <<< "$total_counts <= 0") ))
  then
    echo "Leader sampling [No sample generated]"
    err=1
    return
  fi
  isok=$(bc <<< "scale=2; if (($invalid_counts/$total_counts) < (1-$tolerance_rate)) { 0 } else { 1 };")
  if [ $isok -eq 1 ]
  then
     echo "Leader sampling [Failed inconsistent cycles count]"
     err=1
  else
    echo "Basic leader sampling test [Success]"
  fi
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

test_callgraph() {
  echo "Callgraph test"

  case $(uname -m)
  in s390x)
       cmd_flags="--call-graph dwarf -e cpu-clock";;
     *)
       cmd_flags="-g";;
  esac

  if ! perf record -o "${perfdata}" $cmd_flags perf test -w brstack
  then
    echo "Callgraph test [Failed missing output]"
    err=1
    return
  fi

  if ! perf report -i "${perfdata}" 2>&1 | grep "${testsym2}"
  then
    echo "Callgraph test [Failed missing symbol]"
    err=1
    return
  fi

  echo "Callgraph test [Success]"
}

test_acr_sampling() {
  events="{instructions/period=40000,acr_mask=0x2/u,cycles/period=20000,acr_mask=0x3/u}"
  pebs_events="{instructions/period=40000,acr_mask=0x2/pu,cycles/period=20000,acr_mask=0x3/u}"
  echo "Auto counter reload (ACR) sampling test"
  if ! perf record -o "${perfdata}" -e "${events}" ${testprog} 2> /dev/null
  then
    echo "Auto counter reload sampling [Skipped not supported]"
    return
  fi
  if ! perf script -i "${perfdata}" -F event | grep -q "instructions"
  then
    echo "Auto counter reload sampling [Failed missing instructions event]"
    err=1
    return
  fi
  if perf script -i "${perfdata}" -F event | grep -q "cycles"
  then
    echo "Auto counter reload sampling [Failed cycles event shouldn't be sampled]"
    err=1
    return
  fi
  if ! perf record -o "${perfdata}" -e "${pebs_events}" ${testprog} 2> /dev/null
  then
    echo "Auto counter reload PEBS sampling [Skipped not supported]"
    echo "Auto counter reload sampling [Success]"
    return
  fi
  if ! perf script -i "${perfdata}" -F event | grep -q "instructions"
  then
    echo "Auto counter reload PEBS sampling [Failed missing instructions event]"
    err=1
    return
  fi
  if perf script -i "${perfdata}" -F event | grep -q "cycles"
  then
    echo "Auto counter reload PEBS sampling [Failed cycles event shouldn't be sampled]"
    err=1
    return
  fi
  echo "Auto counter reload sampling [Success]"
}

test_ratio_to_prev() {
  echo "ratio-to-prev test"
  if ! perf record -o /dev/null -e "{instructions, cycles/period=100000,ratio-to-prev=0.5/}" \
     true 2> /dev/null
  then
    echo "ratio-to-prev [Skipped not supported]"
    return
  fi
  if ! perf record -o /dev/null -e "instructions, cycles/period=100000,ratio-to-prev=0.5/" \
     true |& grep -q 'Invalid use of ratio-to-prev term without preceding element in group'
  then
    echo "ratio-to-prev test [Failed elements must be in same group]"
    err=1
    return
  fi
  if ! perf record -o /dev/null -e "{instructions,dummy,cycles/period=100000,ratio-to-prev=0.5/}" \
     true |& grep -q 'must have same PMU'
  then
    echo "ratio-to-prev test [Failed elements must have same PMU]"
    err=1
    return
  fi
  if ! perf record -o /dev/null -e "{instructions,cycles/ratio-to-prev=0.5/}" \
     true |& grep -q 'Event period term or count (-c) must be set when using ratio-to-prev term.'
  then
    echo "ratio-to-prev test [Failed period must be set]"
    err=1
    return
  fi
  if ! perf record -o /dev/null -e "{cycles/ratio-to-prev=0.5/}" \
     true |& grep -q 'Invalid use of ratio-to-prev term without preceding element in group'
  then
    echo "ratio-to-prev test [Failed need 2+ events]"
    err=1
    return
  fi
  echo "Basic ratio-to-prev record test [Success]"
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
test_uid
test_leader_sampling
test_topdown_leader_sampling
test_precise_max
test_callgraph
test_acr_sampling
test_ratio_to_prev

# restore the default value
ulimit -Sn $default_fd_limit

cleanup
exit $err
