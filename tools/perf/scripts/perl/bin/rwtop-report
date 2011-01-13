#!/bin/bash
# description: system-wide r/w top
# args: [interval]
n_args=0
for i in "$@"
do
    if expr match "$i" "-" > /dev/null ; then
	break
    fi
    n_args=$(( $n_args + 1 ))
done
if [ "$n_args" -gt 1 ] ; then
    echo "usage: rwtop-report [interval]"
    exit
fi
if [ "$n_args" -gt 0 ] ; then
    interval=$1
    shift
fi
perf script $@ -s "$PERF_EXEC_PATH"/scripts/perl/rwtop.pl $interval
