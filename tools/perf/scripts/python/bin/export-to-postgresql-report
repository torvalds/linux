#!/bin/bash
# description: export perf data to a postgresql database
# args: [database name] [columns] [calls]
n_args=0
for i in "$@"
do
    if expr match "$i" "-" > /dev/null ; then
	break
    fi
    n_args=$(( $n_args + 1 ))
done
if [ "$n_args" -gt 3 ] ; then
    echo "usage: export-to-postgresql-report [database name] [columns] [calls]"
    exit
fi
if [ "$n_args" -gt 2 ] ; then
    dbname=$1
    columns=$2
    calls=$3
    shift 3
elif [ "$n_args" -gt 1 ] ; then
    dbname=$1
    columns=$2
    shift 2
elif [ "$n_args" -gt 0 ] ; then
    dbname=$1
    shift
fi
perf script $@ -s "$PERF_EXEC_PATH"/scripts/python/export-to-postgresql.py $dbname $columns $calls
