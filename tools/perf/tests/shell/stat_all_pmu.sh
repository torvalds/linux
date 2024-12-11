#!/bin/bash
# perf all PMU test (exclusive)
# SPDX-License-Identifier: GPL-2.0

set -e
err=0
result=""

trap_cleanup() {
  echo "Unexpected signal in ${FUNCNAME[1]}"
  echo "$result"
  exit 1
}
trap trap_cleanup EXIT TERM INT

# Test all PMU events; however exclude parameterized ones (name contains '?')
for p in $(perf list --raw-dump pmu | sed 's/[[:graph:]]\+?[[:graph:]]\+[[:space:]]//g')
do
  echo "Testing $p"
  result=$(perf stat -e "$p" true 2>&1)
  if echo "$result" | grep -q "$p"
  then
    # Event seen in output.
    continue
  fi
  if echo "$result" | grep -q "<not supported>"
  then
    # Event not supported, so ignore.
    continue
  fi
  if echo "$result" | grep -q "Access to performance monitoring and observability operations is limited."
  then
    # Access is limited, so ignore.
    continue
  fi

  # We failed to see the event and it is supported. Possibly the workload was
  # too small so retry with something longer.
  result=$(perf stat -e "$p" perf bench internals synthesize 2>&1)
  if echo "$result" | grep -q "$p"
  then
    # Event seen in output.
    continue
  fi
  echo "Error: event '$p' not printed in:"
  echo "$result"
  err=1
done

trap - EXIT TERM INT
exit $err
