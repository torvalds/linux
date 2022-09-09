#!/bin/sh
# perf all PMU test
# SPDX-License-Identifier: GPL-2.0

set -e

for p in $(perf list --raw-dump pmu); do
  # In powerpc, skip the events for hv_24x7 and hv_gpci.
  # These events needs input values to be filled in for
  # core, chip, partition id based on system.
  # Example: hv_24x7/CPM_ADJUNCT_INST,domain=?,core=?/
  # hv_gpci/event,partition_id=?/
  # Hence skip these events for ppc.
  if echo "$p" |grep -Eq 'hv_24x7|hv_gpci' ; then
    echo "Skipping: Event '$p' in powerpc"
    continue
  fi
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
