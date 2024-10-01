#!/bin/bash

#
# Profiling physical memory by all retired load instructions/uops event
# MEM_INST_RETIRED.ALL_LOADS or MEM_UOPS_RETIRED.ALL_LOADS
#

load=`perf list | grep mem_inst_retired.all_loads`
if [ -z "$load" ]; then
	load=`perf list | grep mem_uops_retired.all_loads`
fi
if [ -z "$load" ]; then
	echo "There is no event to count all retired load instructions/uops."
	exit 1
fi

arg=$(echo $load | tr -d ' ')
arg="$arg:P"
perf record --phys-data -e $arg $@
