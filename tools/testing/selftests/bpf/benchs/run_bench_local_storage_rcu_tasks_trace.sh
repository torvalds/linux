#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

kthread_pid=`pgrep rcu_tasks_trace_kthread`

if [ -z $kthread_pid ]; then
	echo "error: Couldn't find rcu_tasks_trace_kthread"
	exit 1
fi

./bench --nr_procs 15000 --kthread_pid $kthread_pid -d 600 --quiet 1 local-storage-tasks-trace
