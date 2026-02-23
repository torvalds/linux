#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

if [ $EUID -ne 0 ]
then
	echo "Run as root"
	exit $ksft_skip
fi

damon_sysfs="/sys/kernel/mm/damon/admin"
if [ ! -d "$damon_sysfs" ]
then
	echo "damon sysfs not found"
	exit $ksft_skip
fi

kmemleak="/sys/kernel/debug/kmemleak"
if [ ! -f "$kmemleak" ]
then
	echo "$kmemleak not found"
	exit $ksft_skip
fi

# ensure filter directory
echo 1 > "$damon_sysfs/kdamonds/nr_kdamonds"
echo 1 > "$damon_sysfs/kdamonds/0/contexts/nr_contexts"
echo 1 > "$damon_sysfs/kdamonds/0/contexts/0/schemes/nr_schemes"
echo 1 > "$damon_sysfs/kdamonds/0/contexts/0/schemes/0/filters/nr_filters"

filter_dir="$damon_sysfs/kdamonds/0/contexts/0/schemes/0/filters/0"

# try to leak 128 times
for i in {1..128};
do
	echo "012345678901234567890123456789" > "$filter_dir/memcg_path"
done

echo scan > "$kmemleak"
kmemleak_report=$(cat "$kmemleak")
if [ "$kmemleak_report" = "" ]
then
	exit 0
fi
echo "$kmemleak_report"
exit 1
