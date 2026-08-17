#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
. "$(dirname $0)/lib/get_workload_pids.sh"
rtla_pid=$(echo $(ps -o ppid= $$))
rtla_cgroup=$(</proc/$rtla_pid/cgroup)
echo "RTLA cgroup: $rtla_cgroup"
for pid in $(get_workload_pids)
do
    pid_cgroup=$(</proc/$pid/cgroup)
    echo "PID $pid cgroup: $pid_cgroup"
    if ! [ "$pid_cgroup" = "$rtla_cgroup" ]
    then
        echo "Mismatch!"
        exit 0
    fi
done
echo "cgroup matches for all workload PIDs"
