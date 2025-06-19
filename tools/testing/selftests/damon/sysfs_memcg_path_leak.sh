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

# ensure filter directory
echo 1 > "$damon_sysfs/kdamonds/nr_kdamonds"
echo 1 > "$damon_sysfs/kdamonds/0/contexts/nr_contexts"
echo 1 > "$damon_sysfs/kdamonds/0/contexts/0/schemes/nr_schemes"
echo 1 > "$damon_sysfs/kdamonds/0/contexts/0/schemes/0/filters/nr_filters"

filter_dir="$damon_sysfs/kdamonds/0/contexts/0/schemes/0/filters/0"

before_kb=$(grep Slab /proc/meminfo | awk '{print $2}')

# try to leak 3000 KiB
for i in {1..102400};
do
	echo "012345678901234567890123456789" > "$filter_dir/memcg_path"
done

after_kb=$(grep Slab /proc/meminfo | awk '{print $2}')
# expect up to 1500 KiB free from other tasks memory
expected_after_kb_max=$((before_kb + 1500))

if [ "$after_kb" -gt "$expected_after_kb_max" ]
then
	echo "maybe memcg_path are leaking: $before_kb -> $after_kb"
	exit 1
else
	exit 0
fi
