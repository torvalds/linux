#!/bin/bash
# perf all metrics test
# SPDX-License-Identifier: GPL-2.0

ParanoidAndNotRoot()
{
  [ "$(id -u)" != 0 ] && [ "$(cat /proc/sys/kernel/perf_event_paranoid)" -gt $1 ]
}

test_prog="true"
system_wide_flag="-a"
if ParanoidAndNotRoot 0
then
  system_wide_flag=""
  test_prog="perf test -w noploop 0.01"
fi

check_metric() {
  local output="$1"
  local status="$2"
  local metric="$3"

  if [[ $status -ne 0 || ! "$output" =~ ${metric:0:50} ]]; then
    return 1
  fi

  if [[ "$output" =~ "<not counted>" || "$output" =~ "<not supported>" ]]; then
    return 1
  fi

  return 0
}

skip=0
err=3
for m in $(perf list --raw-dump metrics); do
  echo "Testing $m"
  result=$(perf stat -M "$m" $system_wide_flag -- $test_prog 2>&1)
  result_err=$?

  if check_metric "$result" $result_err "$m"; then
    if [[ "$err" -ne 1 ]]
    then
      err=0
    fi
    continue
  fi

  if [[ "$result" =~ "Access to performance monitoring and observability operations is limited" || \
        "$result" =~ "in per-thread mode, enable system wide" || \
        "$result" =~ "<not supported>" || \
        "$result" =~ "Cannot resolve IDs for" || \
        "$result" =~ "No supported events found" || \
        "$result" =~ "FP_ARITH" || \
        "$result" =~ "AMX" || \
        "$result" =~ "PMM" ]]
  then
    true
  else
    result=$(perf stat -M "$m" $system_wide_flag -- perf test -w noploop 0.1 2>&1)
    result_err=$?

    if check_metric "$result" $result_err "$m"; then
      if [[ "$err" -ne 1 ]]
      then
        err=0
      fi
      continue
    fi
  fi

  # If retry also failed, determine if we skip, ignore, or fail
  if [[ "$result" =~ "Access to performance monitoring and observability operations is limited" ]]
  then
    echo "[Skipped $m] Permission failure"
    echo $result
    if [[ $err -eq 0 ]]
    then
      skip=1
    fi
    continue
  elif [[ "$result" =~ "in per-thread mode, enable system wide" ]]
  then
    echo "[Skipped $m] Permissions - need system wide mode"
    echo $result
    if [[ $err -eq 0 ]]
    then
      skip=1
    fi
    continue
  elif [[ "$result" =~ "<not supported>" || \
          "$result" =~ "Cannot resolve IDs for" || \
          "$result" =~ "No supported events found" ]]
  then
    if [[ $(perf list --raw-dump $m) == "Default"* ]]
    then
      echo "[Ignored $m] failed but as a Default metric this can be expected"
      echo $result
      continue
    fi
    echo "[Skipped $m] Not supported events"
    echo $result
    if [[ $err -eq 0 ]]
    then
      skip=1
    fi
    continue
  elif [[ "$result" =~ "<not counted>" ]]
  then
    echo "[Skipped $m] Not counted events"
    echo $result
    if [[ $err -eq 0 ]]
    then
      skip=1
    fi
    continue
  elif [[ "$result" =~ "FP_ARITH" || "$result" =~ "AMX" ]]
  then
    echo "[Skipped $m] FP issues"
    echo $result
    if [[ $err -eq 0 ]]
    then
      skip=1
    fi
    continue
  elif [[ "$result" =~ "PMM" ]]
  then
    echo "[Skipped $m] Optane memory issues"
    echo $result
    if [[ $err -eq 0 ]]
    then
      skip=1
    fi
    continue
  fi

  echo "[Failed $m] has non-zero error '$result_err' or not printed/counted in:"
  echo "$result"
  err=1
done

# return SKIP only if no success returned
if [[ "$err" -eq 3 && "$skip" -eq 1 ]]
then
  err=2
fi

exit "$err"
