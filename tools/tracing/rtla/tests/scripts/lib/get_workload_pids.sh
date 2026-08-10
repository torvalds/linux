#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
get_workload_pids() {
    local shell_pid=$$
    local rtla_pid=$(ps -o ppid= $shell_pid)

    # kernel threads
    pgrep -P $(pgrep ^kthreadd$) -f '^\[?(osnoise|timerlat)/[0-9]+\]?$'
    # user threads
    pgrep -P $rtla_pid | grep -v "^$shell_pid$"
}
