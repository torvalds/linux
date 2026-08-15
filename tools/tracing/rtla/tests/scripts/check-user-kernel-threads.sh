#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
. "$(dirname $0)/lib/get_workload_pids.sh"
kthreadd_pid=$(pgrep ^kthreadd$)
cnt_kernel=0
cnt_user=0
for pid in $(get_workload_pids)
do
    if [ "$(echo $(ps -o ppid= $pid))" = "$kthreadd_pid" ]
    then
        ((++cnt_kernel))
    else
        ((++cnt_user))
    fi
done
echo "$cnt_kernel kernel threads, $cnt_user user threads"
