#!/bin/bash
# perf all PMU test (exclusive)
# SPDX-License-Identifier: GPL-2.0

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
  echo -n "Testing $p -- "
  output=$(perf stat -e "$p" true 2>&1)
  stat_result=$?
  if echo "$output" | grep -q "$p"
  then
    # Event seen in output.
    if [ $stat_result -eq 0 ] && ! echo "$output" | grep -q "<not supported>"
    then
      # Event supported.
      echo "supported"
      continue
    elif echo "$output" | grep -q "<not supported>"
    then
      # Event not supported, so ignore.
      echo "not supported"
      continue
    elif echo "$output" | grep -q "No permission to enable"
    then
      # No permissions, so ignore.
      echo "no permission to enable"
      continue
    elif echo "$output" | grep -q "Bad event name"
    then
      # Non-existent event.
      echo "Error: Bad event name"
      echo "$output"
      err=1
      continue
    fi
  fi

  if echo "$output" | grep -q "Access to performance monitoring and observability operations is limited."
  then
    # Access is limited, so ignore.
    echo "access limited"
    continue
  fi

  # We failed to see the event and it is supported. Possibly the workload was
  # too small so retry with something longer.
  output=$(perf stat -e "$p" perf bench internals synthesize 2>&1)
  if echo "$output" | grep -q "$p"
  then
    # Event seen in output.
    echo "supported"
    continue
  fi
  echo "Error: event '$p' not printed in:"
  echo "$output"
  err=1
done

trap - EXIT TERM INT
exit $err
