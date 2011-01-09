#!/bin/bash
# description: syscall top
# args: [comm] [interval]
n_args=0
for i in "$@"
do
    if expr match "$i" "-" > /dev/null ; then
	break
    fi
    n_args=$(( $n_args + 1 ))
done
if [ "$n_args" -gt 2 ] ; then
    echo "usage: sctop-report [comm] [interval]"
    exit
fi
if [ "$n_args" -gt 1 ] ; then
    comm=$1
    interval=$2
    shift 2
elif [ "$n_args" -gt 0 ] ; then
    interval=$1
    shift
fi
perf script $@ -s "$PERF_EXEC_PATH"/scripts/python/sctop.py $comm $interval
