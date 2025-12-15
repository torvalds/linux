#!/bin/bash
# record weak terms
# SPDX-License-Identifier: GPL-2.0
# Test that command line options override weak terms from sysfs or inbuilt json.
set -e

shelldir=$(dirname "$0")
# shellcheck source=lib/setup_python.sh
. "${shelldir}"/lib/setup_python.sh

# Find the first event with a specified period, such as
# "cpu_core/event=0x24,period=200003,umask=0xff/"
event=$(perf list --json | $PYTHON -c '
import json, sys
for e in json.load(sys.stdin):
    if "EventName" not in e or "/modifier" in e["EventName"]:
        continue
    if "Encoding" in e and "period=" in e["Encoding"]:
        print(e["EventName"])
        break
')
if [[ "$event" = "" ]]
then
  echo "Skip: No sysfs/json events with inbuilt period."
  exit 2
fi

echo "Testing that for $event the period is overridden with 1000"
perf list --detail "$event"
if ! perf record -c 1000 -vv -e "$event" -o /dev/null true 2>&1 | \
  grep -q -F '{ sample_period, sample_freq }   1000'
then
  echo "Fail: Unexpected verbose output and sample period"
  exit 1
fi
echo "Success"
exit 0
