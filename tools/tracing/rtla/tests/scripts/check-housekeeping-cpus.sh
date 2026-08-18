#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
pid=$(ps -o ppid= $$)
echo "Affinity of threads:$(taskset -c -p $pid | cut -d ':' -f 2)"
