#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: trace_marker trigger - test snapshot trigger
# requires: set_event snapshot events/ftrace/print/trigger
# flags: instance

fail() { #msg
    echo $1
    exit_fail
}

test_trace() {
    file=$1
    x=$2

    cat $file | while read line; do
	comment=`echo $line | sed -e 's/^#//'`
	if [ "$line" != "$comment" ]; then
	    continue
	fi
	echo "testing $line for >$x<"
	match=`echo $line | sed -e "s/>$x<//"`
	if [ "$line" = "$match" ]; then
	    fail "$line does not have >$x< in it"
	fi
	x=$((x+2))
    done
}

echo "Test snapshot trace_marker trigger"

echo 'snapshot' > events/ftrace/print/trigger

# make sure the snapshot is allocated

grep -q 'Snapshot is allocated' snapshot

for i in `seq 1 10` ; do echo "hello >$i<" > trace_marker; done

test_trace trace 1
test_trace snapshot 2

exit 0
