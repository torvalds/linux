#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2015-2019 ARM Limited.
# Original author: Dave Martin <Dave.Martin@arm.com>

set -ue

NR_CPUS=`nproc`

pids=
logs=

cleanup () {
	trap - INT TERM CHLD
	set +e

	if [ -n "$pids" ]; then
		kill $pids
		wait $pids
		pids=
	fi

	if [ -n "$logs" ]; then
		cat $logs
		rm $logs
		logs=
	fi
}

interrupt () {
	cleanup
	exit 0
}

child_died () {
	cleanup
	exit 1
}

trap interrupt INT TERM EXIT

for x in `seq 0 $((NR_CPUS * 4))`; do
	log=`mktemp`
	logs=$logs\ $log
	./za-test >$log &
	pids=$pids\ $!
done

# Wait for all child processes to be created:
sleep 10

while :; do
	kill -USR1 $pids
done &
pids=$pids\ $!

wait

exit 1
