#!/bin/sh
# perf all metrics test
# SPDX-License-Identifier: GPL-2.0

set -e

err=0
for m in $(perf list --raw-dump metrics); do
  echo "Testing $m"
  result=$(perf stat -M "$m" true 2>&1)
  if [[ ! "$result" =~ "$m" ]] && [[ ! "$result" =~ "<not supported>" ]]; then
    # We failed to see the metric and the events are support. Possibly the
    # workload was too small so retry with something longer.
    result=$(perf stat -M "$m" perf bench internals synthesize 2>&1)
    if [[ ! "$result" =~ "$m" ]]; then
      echo "Metric '$m' not printed in:"
      echo "$result"
      if [[ "$result" =~ "FP_ARITH" && "$err" != "1" ]]; then
        echo "Skip, not fail, for FP issues"
        err=2
      else
        err=1
      fi
    fi
  fi
done

exit "$err"
