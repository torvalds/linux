#!/bin/bash
# Copyright (c) 2015 Oracle and/or its affiliates. All Rights Reserved.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation; either version 2 of
# the License, or (at your option) any later version.
#
# This program is distributed in the hope that it would be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
# GNU General Public License for more details.
#
# Test creates several zram devices with different filesystems on them.
# It fills each device with zeros and checks that compression works.
#
# Author: Alexey Kodanev <alexey.kodanev@oracle.com>
# Modified: Naresh Kamboju <naresh.kamboju@linaro.org>

TCID="zram01"
ERR_CODE=0

. ./zram_lib.sh

# Test will create the following number of zram devices:
dev_num=1
# This is a list of parameters for zram devices.
# Number of items must be equal to 'dev_num' parameter.
zram_max_streams="2"

# The zram sysfs node 'disksize' value can be either in bytes,
# or you can use mem suffixes. But in some old kernels, mem
# suffixes are not supported, for example, in RHEL6.6GA's kernel
# layer, it uses strict_strtoull() to parse disksize which does
# not support mem suffixes, in some newer kernels, they use
# memparse() which supports mem suffixes. So here we just use
# bytes to make sure everything works correctly.
zram_sizes="2097152" # 2MB
zram_mem_limits="2M"
zram_filesystems="ext4"
zram_algs="lzo"

zram_fill_fs()
{
	local mem_free0=$(free -m | awk 'NR==2 {print $4}')

	for i in $(seq 0 $(($dev_num - 1))); do
		echo "fill zram$i..."
		local b=0
		while [ true ]; do
			dd conv=notrunc if=/dev/zero of=zram${i}/file \
				oflag=append count=1 bs=1024 status=none \
				> /dev/null 2>&1 || break
			b=$(($b + 1))
		done
		echo "zram$i can be filled with '$b' KB"
	done

	local mem_free1=$(free -m | awk 'NR==2 {print $4}')
	local used_mem=$(($mem_free0 - $mem_free1))

	local total_size=0
	for sm in $zram_sizes; do
		local s=$(echo $sm | sed 's/M//')
		total_size=$(($total_size + $s))
	done

	echo "zram used ${used_mem}M, zram disk sizes ${total_size}M"

	local v=$((100 * $total_size / $used_mem))

	if [ "$v" -lt 100 ]; then
		echo "FAIL compression ratio: 0.$v:1"
		ERR_CODE=-1
		zram_cleanup
		return
	fi

	echo "zram compression ratio: $(echo "scale=2; $v / 100 " | bc):1: OK"
}

check_prereqs
zram_load
zram_max_streams
zram_compress_alg
zram_set_disksizes
zram_set_memlimit
zram_makefs
zram_mount

zram_fill_fs
zram_cleanup
zram_unload

if [ $ERR_CODE -ne 0 ]; then
	echo "$TCID : [FAIL]"
else
	echo "$TCID : [PASS]"
fi
