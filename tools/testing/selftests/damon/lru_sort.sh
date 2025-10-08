#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

source _common.sh

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

check_dependencies

damon_lru_sort_enabled="/sys/module/damon_lru_sort/parameters/enabled"
if [ ! -f "$damon_lru_sort_enabled" ]
then
	echo "No 'enabled' file.  Maybe DAMON_LRU_SORT not built"
	exit $ksft_skip
fi

nr_kdamonds=$(pgrep kdamond | wc -l)
if [ "$nr_kdamonds" -ne 0 ]
then
	echo "Another kdamond is running"
	exit $ksft_skip
fi

echo Y > "$damon_lru_sort_enabled"
nr_kdamonds=$(pgrep kdamond | wc -l)
if [ "$nr_kdamonds" -ne 1 ]
then
	echo "kdamond is not turned on"
	exit 1
fi

echo N > "$damon_lru_sort_enabled"
nr_kdamonds=$(pgrep kdamond | wc -l)
if [ "$nr_kdamonds" -ne 0 ]
then
	echo "kdamond is not turned off"
	exit 1
fi
