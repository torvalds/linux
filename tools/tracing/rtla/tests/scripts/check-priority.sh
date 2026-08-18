#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
. "$(dirname $0)/lib/get_workload_pids.sh"
for pid in $(get_workload_pids)
do
  chrt -p $pid | cut -d ':' -f 2 | head -n1 | grep "^ $1\$" >/dev/null
  chrt -p $pid | cut -d ':' -f 2 | tail -n1 | grep "^ $2\$" >/dev/null
done && echo "Priorities are set correctly"
