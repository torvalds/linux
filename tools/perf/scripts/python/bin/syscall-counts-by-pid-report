#!/bin/bash
# description: system-wide syscall counts, by pid
# args: [comm]
if [ $# -gt 0 ] ; then
    if ! expr match "$1" "-" > /dev/null ; then
	comm=$1
	shift
    fi
fi
perf script $@ -s "$PERF_EXEC_PATH"/scripts/python/syscall-counts-by-pid.py $comm
