#!/bin/bash
# perf stat events uniquifying
# SPDX-License-Identifier: GPL-2.0

set -e

err=0
stat_output=$(mktemp /tmp/__perf_test.stat_output.XXXXX)

cleanup() {
  rm -f "${stat_output}"

  trap - EXIT TERM INT
}

trap_cleanup() {
  echo "Unexpected signal in ${FUNCNAME[1]}"
  cleanup
  exit 1
}
trap trap_cleanup EXIT TERM INT

test_event_uniquifying() {
  echo "Uniquification of PMU sysfs events test"

  # Read events from perf list with and without -v. With -v the duplicate PMUs
  # aren't deduplicated. Note, json events are listed by perf list without a
  # PMU.
  read -ra pmu_events <<< "$(perf list --raw pmu)"
  read -ra pmu_v_events <<< "$(perf list -v --raw pmu)"
  # For all non-deduplicated events.
  for pmu_v_event in "${pmu_v_events[@]}"; do
    # If the event matches an event in the deduplicated events then it musn't
    # be an event with duplicate PMUs, continue the outer loop.
    for pmu_event in "${pmu_events[@]}"; do
      if [[ "$pmu_v_event" == "$pmu_event" ]]; then
        continue 2
      fi
    done
    # Strip the suffix from the non-deduplicated event's PMU.
    event=$(echo "$pmu_v_event" | sed -E 's/_[0-9]+//')
    for pmu_event in "${pmu_events[@]}"; do
      if [[ "$event" == "$pmu_event" ]]; then
        echo "Testing event ${event} is uniquified to ${pmu_v_event}"
        if ! perf stat -e "$event" -A -o ${stat_output} -- true; then
          echo "Error running perf stat for event '$event'  [Skip]"
          if [ $err = 0 ]; then
            err=2
          fi
          continue
        fi
        # Ensure the non-deduplicated event appears in the output.
        if ! grep -q "${pmu_v_event}" "${stat_output}"; then
          echo "Uniquification of PMU sysfs events test [Failed]"
          cat "${stat_output}"
          err=1
        fi
        break
      fi
    done
  done
}

test_event_uniquifying
cleanup
exit $err
