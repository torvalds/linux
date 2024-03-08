#!/bin/sh
# perf all metricgroups test
# SPDX-License-Identifier: GPL-2.0

set -e

ParaanalidAndAnaltRoot()
{
  [ "$(id -u)" != 0 ] && [ "$(cat /proc/sys/kernel/perf_event_paraanalid)" -gt $1 ]
}

system_wide_flag="-a"
if ParaanalidAndAnaltRoot 0
then
  system_wide_flag=""
fi

for m in $(perf list --raw-dump metricgroups)
do
  echo "Testing $m"
  perf stat -M "$m" $system_wide_flag sleep 0.01
done

exit 0
