#!/bin/bash
# test Intel TPEBS counting mode (exclusive)
# SPDX-License-Identifier: GPL-2.0

set -e
grep -q GenuineIntel /proc/cpuinfo || { echo Skipping non-Intel; exit 2; }

# Use this event for testing because it should exist in all platforms
event=cache-misses:R

# Hybrid platforms output like "cpu_atom/cache-misses/R", rather than as above
alt_name=/cache-misses/R

# Without this cmd option, default value or zero is returned
#echo "Testing without --record-tpebs"
#result=$(perf stat -e "$event" true 2>&1)
#[[ "$result" =~ $event || "$result" =~ $alt_name ]] || exit 1

# In platforms that do not support TPEBS, it should execute without error.
echo "Testing with --record-tpebs"
result=$(perf stat -e "$event" --record-tpebs -a sleep 0.01 2>&1)
[[ "$result" =~ "perf record" && "$result" =~ $event || "$result" =~ $alt_name ]] || exit 1
