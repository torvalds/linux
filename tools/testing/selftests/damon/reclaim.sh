#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

if [ $EUID -ne 0 ]
then
	echo "Run as root"
	exit $ksft_skip
fi

damon_reclaim_enabled="/sys/module/damon_reclaim/parameters/enabled"
if [ ! -f "$damon_reclaim_enabled" ]
then
	echo "No 'enabled' file.  Maybe DAMON_RECLAIM not built"
	exit $ksft_skip
fi

nr_kdamonds=$(pgrep kdamond | wc -l)
if [ "$nr_kdamonds" -ne 0 ]
then
	echo "Another kdamond is running"
	exit $ksft_skip
fi

echo Y > "$damon_reclaim_enabled"

nr_kdamonds=$(pgrep kdamond | wc -l)
if [ "$nr_kdamonds" -ne 1 ]
then
	echo "kdamond is not turned on"
	exit 1
fi

echo N > "$damon_reclaim_enabled"
nr_kdamonds=$(pgrep kdamond | wc -l)
if [ "$nr_kdamonds" -ne 0 ]
then
	echo "kdamond is not turned off"
	exit 1
fi
