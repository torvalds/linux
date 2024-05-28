#!/bin/sh
# perf all PMU test
# SPDX-License-Identifier: GPL-2.0

set -e

# Test all PMU events; however exclude parameterized ones (name contains '?')
for p in $(perf list --raw-dump pmu | sed 's/[[:graph:]]\+?[[:graph:]]\+[[:space:]]//g'); do
  echo "Testing $p"
  result=$(perf stat -e "$p" true 2>&1)
  if ! echo "$result" | grep -q "$p" && ! echo "$result" | grep -q "<not supported>" ; then
    # We failed to see the event and it is supported. Possibly the workload was
    # too small so retry with something longer.
    result=$(perf stat -e "$p" perf bench internals synthesize 2>&1)
    if ! echo "$result" | grep -q "$p" ; then
      echo "Event '$p' not printed in:"
      echo "$result"
      exit 1
    fi
  fi
done

exit 0
