#!/bin/sh
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
# Author: Alexey Kodanev <alexey.kodanev@oracle.com>
# Modified: Naresh Kamboju <naresh.kamboju@linaro.org>

MODULE=0
dev_makeswap=-1
dev_mounted=-1

trap INT

check_prereqs()
{
	local msg="skip all tests:"
	local uid=$(id -u)

	if [ $uid -ne 0 ]; then
		echo $msg must be run as root >&2
		exit 0
	fi
}

zram_cleanup()
{
	echo "zram cleanup"
	local i=
	for i in $(seq 0 $dev_makeswap); do
		swapoff /dev/zram$i
	done

	for i in $(seq 0 $dev_mounted); do
		umount /dev/zram$i
	done

	for i in $(seq 0 $(($dev_num - 1))); do
		echo 1 > /sys/block/zram${i}/reset
		rm -rf zram$i
	done

}

zram_unload()
{
	if [ $MODULE -ne 0 ] ; then
		echo "zram rmmod zram"
		rmmod zram > /dev/null 2>&1
	fi
}

zram_load()
{
	# check zram module exists
	MODULE_PATH=/lib/modules/`uname -r`/kernel/drivers/block/zram/zram.ko
	if [ -f $MODULE_PATH ]; then
		MODULE=1
		echo "create '$dev_num' zram device(s)"
		modprobe zram num_devices=$dev_num
		if [ $? -ne 0 ]; then
			echo "failed to insert zram module"
			exit 1
		fi

		dev_num_created=$(ls /dev/zram* | wc -w)

		if [ "$dev_num_created" -ne "$dev_num" ]; then
			echo "unexpected num of devices: $dev_num_created"
			ERR_CODE=-1
		else
			echo "zram load module successful"
		fi
	elif [ -b /dev/zram0 ]; then
		echo "/dev/zram0 device file found: OK"
	else
		echo "ERROR: No zram.ko module or no /dev/zram0 device found"
		echo "$TCID : CONFIG_ZRAM is not set"
		exit 1
	fi
}

zram_max_streams()
{
	echo "set max_comp_streams to zram device(s)"

	local i=0
	for max_s in $zram_max_streams; do
		local sys_path="/sys/block/zram${i}/max_comp_streams"
		echo $max_s > $sys_path || \
			echo "FAIL failed to set '$max_s' to $sys_path"
		sleep 1
		local max_streams=$(cat $sys_path)

		[ "$max_s" -ne "$max_streams" ] && \
			echo "FAIL can't set max_streams '$max_s', get $max_stream"

		i=$(($i + 1))
		echo "$sys_path = '$max_streams' ($i/$dev_num)"
	done

	echo "zram max streams: OK"
}

zram_compress_alg()
{
	echo "test that we can set compression algorithm"

	local algs=$(cat /sys/block/zram0/comp_algorithm)
	echo "supported algs: $algs"
	local i=0
	for alg in $zram_algs; do
		local sys_path="/sys/block/zram${i}/comp_algorithm"
		echo "$alg" >	$sys_path || \
			echo "FAIL can't set '$alg' to $sys_path"
		i=$(($i + 1))
		echo "$sys_path = '$alg' ($i/$dev_num)"
	done

	echo "zram set compression algorithm: OK"
}

zram_set_disksizes()
{
	echo "set disk size to zram device(s)"
	local i=0
	for ds in $zram_sizes; do
		local sys_path="/sys/block/zram${i}/disksize"
		echo "$ds" >	$sys_path || \
			echo "FAIL can't set '$ds' to $sys_path"

		i=$(($i + 1))
		echo "$sys_path = '$ds' ($i/$dev_num)"
	done

	echo "zram set disksizes: OK"
}

zram_set_memlimit()
{
	echo "set memory limit to zram device(s)"

	local i=0
	for ds in $zram_mem_limits; do
		local sys_path="/sys/block/zram${i}/mem_limit"
		echo "$ds" >	$sys_path || \
			echo "FAIL can't set '$ds' to $sys_path"

		i=$(($i + 1))
		echo "$sys_path = '$ds' ($i/$dev_num)"
	done

	echo "zram set memory limit: OK"
}

zram_makeswap()
{
	echo "make swap with zram device(s)"
	local i=0
	for i in $(seq 0 $(($dev_num - 1))); do
		mkswap /dev/zram$i > err.log 2>&1
		if [ $? -ne 0 ]; then
			cat err.log
			echo "FAIL mkswap /dev/zram$1 failed"
		fi

		swapon /dev/zram$i > err.log 2>&1
		if [ $? -ne 0 ]; then
			cat err.log
			echo "FAIL swapon /dev/zram$1 failed"
		fi

		echo "done with /dev/zram$i"
		dev_makeswap=$i
	done

	echo "zram making zram mkswap and swapon: OK"
}

zram_swapoff()
{
	local i=
	for i in $(seq 0 $dev_makeswap); do
		swapoff /dev/zram$i > err.log 2>&1
		if [ $? -ne 0 ]; then
			cat err.log
			echo "FAIL swapoff /dev/zram$i failed"
		fi
	done
	dev_makeswap=-1

	echo "zram swapoff: OK"
}

zram_makefs()
{
	local i=0
	for fs in $zram_filesystems; do
		# if requested fs not supported default it to ext2
		which mkfs.$fs > /dev/null 2>&1 || fs=ext2

		echo "make $fs filesystem on /dev/zram$i"
		mkfs.$fs /dev/zram$i > err.log 2>&1
		if [ $? -ne 0 ]; then
			cat err.log
			echo "FAIL failed to make $fs on /dev/zram$i"
		fi
		i=$(($i + 1))
		echo "zram mkfs.$fs: OK"
	done
}

zram_mount()
{
	local i=0
	for i in $(seq 0 $(($dev_num - 1))); do
		echo "mount /dev/zram$i"
		mkdir zram$i
		mount /dev/zram$i zram$i > /dev/null || \
			echo "FAIL mount /dev/zram$i failed"
		dev_mounted=$i
	done

	echo "zram mount of zram device(s): OK"
}
