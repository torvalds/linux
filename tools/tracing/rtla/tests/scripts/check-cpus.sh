#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
. "$(dirname $0)/lib/get_workload_pids.sh"
echo -n "Affinity of threads: "
for pid in $(get_workload_pids)
do
    echo -n $(taskset -c -p $pid | cut -d ':' -f 2)
done
echo
