#!/bin/bash
# perf all metricgroups test
# SPDX-License-Identifier: GPL-2.0

ParanoidAndNotRoot()
{
  [ "$(id -u)" != 0 ] && [ "$(cat /proc/sys/kernel/perf_event_paranoid)" -gt $1 ]
}

system_wide_flag="-a"
if ParanoidAndNotRoot 0
then
  system_wide_flag=""
fi
err=0
for m in $(perf list --raw-dump metricgroups)
do
  echo "Testing $m"
  result=$(perf stat -M "$m" $system_wide_flag sleep 0.01 2>&1)
  result_err=$?
  if [[ $result_err -gt 0 ]]
  then
    if [[ "$result" =~ \
          "Access to performance monitoring and observability operations is limited" ]]
    then
      echo "Permission failure"
      echo $result
      if [[ $err -eq 0 ]]
      then
        err=2 # Skip
      fi
    elif [[ "$result" =~ "in per-thread mode, enable system wide" ]]
    then
      echo "Permissions - need system wide mode"
      echo $result
      if [[ $err -eq 0 ]]
      then
        err=2 # Skip
      fi
    else
      echo "Metric group $m failed"
      echo $result
      err=1 # Fail
    fi
  fi
done

exit $err
