#!/bin/bash
# perf stat events uniquifying
# SPDX-License-Identifier: GPL-2.0

set -e

stat_output=$(mktemp /tmp/__perf_test.stat_output.XXXXX)
perf_tool=perf
err=0

test_event_uniquifying() {
  # We use `clockticks` in `uncore_imc` to verify the uniquify behavior.
  pmu="uncore_imc"
  event="clockticks"

  # If the `-A` option is added, the event should be uniquified.
  #
  # $perf list -v clockticks
  #
  # List of pre-defined events (to be used in -e or -M):
  #
  #   uncore_imc_0/clockticks/                           [Kernel PMU event]
  #   uncore_imc_1/clockticks/                           [Kernel PMU event]
  #   uncore_imc_2/clockticks/                           [Kernel PMU event]
  #   uncore_imc_3/clockticks/                           [Kernel PMU event]
  #   uncore_imc_4/clockticks/                           [Kernel PMU event]
  #   uncore_imc_5/clockticks/                           [Kernel PMU event]
  #
  #   ...
  #
  # $perf stat -e clockticks -A -- true
  #
  #  Performance counter stats for 'system wide':
  #
  # CPU0            3,773,018      uncore_imc_0/clockticks/
  # CPU0            3,609,025      uncore_imc_1/clockticks/
  # CPU0                    0      uncore_imc_2/clockticks/
  # CPU0            3,230,009      uncore_imc_3/clockticks/
  # CPU0            3,049,897      uncore_imc_4/clockticks/
  # CPU0                    0      uncore_imc_5/clockticks/
  #
  #        0.002029828 seconds time elapsed

  echo "stat event uniquifying test"
  uniquified_event_array=()

  # Skip if the machine does not have `uncore_imc` device.
  if ! ${perf_tool} list pmu | grep -q ${pmu}; then
    echo "Target does not support PMU ${pmu} [Skipped]"
    err=2
    return
  fi

  # Check how many uniquified events.
  while IFS= read -r line; do
    uniquified_event=$(echo "$line" | awk '{print $1}')
    uniquified_event_array+=("${uniquified_event}")
  done < <(${perf_tool} list -v ${event} | grep ${pmu})

  perf_command="${perf_tool} stat -e $event -A -o ${stat_output} -- true"
  $perf_command

  # Check the output contains all uniquified events.
  for uniquified_event in "${uniquified_event_array[@]}"; do
    if ! cat "${stat_output}" | grep -q "${uniquified_event}"; then
      echo "Event is not uniquified [Failed]"
      echo "${perf_command}"
      cat "${stat_output}"
      err=1
      break
    fi
  done
}

test_event_uniquifying
rm -f "${stat_output}"
exit $err
