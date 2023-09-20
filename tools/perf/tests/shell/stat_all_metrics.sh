#!/bin/bash
# perf all metrics test
# SPDX-License-Identifier: GPL-2.0

err=0
for m in $(perf list --raw-dump metrics); do
  echo "Testing $m"
  result=$(perf stat -M "$m" true 2>&1)
  if [[ "$result" =~ ${m:0:50} ]] || [[ "$result" =~ "<not supported>" ]]
  then
    continue
  fi
  # Failed so try system wide.
  result=$(perf stat -M "$m" -a sleep 0.01 2>&1)
  if [[ "$result" =~ ${m:0:50} ]]
  then
    continue
  fi
  # Failed again, possibly the workload was too small so retry with something
  # longer.
  result=$(perf stat -M "$m" perf bench internals synthesize 2>&1)
  if [[ "$result" =~ ${m:0:50} ]]
  then
    continue
  fi
  echo "Metric '$m' not printed in:"
  echo "$result"
  if [[ "$err" != "1" ]]
  then
    err=2
    if [[ "$result" =~ "FP_ARITH" || "$result" =~ "AMX" ]]
    then
      echo "Skip, not fail, for FP issues"
    elif [[ "$result" =~ "PMM" ]]
    then
      echo "Skip, not fail, for Optane memory issues"
    else
      err=1
    fi
  fi
done

exit "$err"
