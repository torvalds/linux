#!/bin/sh
# perf all PMU test
# SPDX-License-Identifier: GPL-2.0

set -e

for p in $(perf list --raw-dump pmu); do
  echo "Testing $p"
  result=$(perf stat -e "$p" true 2>&1)
  if [[ ! "$result" =~ "$p" ]] && [[ ! "$result" =~ "<not supported>" ]]; then
    # We failed to see the event and it is supported. Possibly the workload was
    # too small so retry with something longer.
    result=$(perf stat -e "$p" perf bench internals synthesize 2>&1)
    if [[ ! "$result" =~ "$p" ]]; then
      echo "Event '$p' not printed in:"
      echo "$result"
      exit 1
    fi
  fi
done

exit 0
