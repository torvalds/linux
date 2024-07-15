#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only

source ../../../net/net_helper.sh

NSIM_DEV_1_ID=$((256 + RANDOM % 256))
NSIM_DEV_1_SYS=/sys/bus/netdevsim/devices/netdevsim$NSIM_DEV_1_ID
NSIM_DEV_2_ID=$((512 + RANDOM % 256))
NSIM_DEV_2_SYS=/sys/bus/netdevsim/devices/netdevsim$NSIM_DEV_2_ID

NSIM_DEV_SYS_NEW=/sys/bus/netdevsim/new_device
NSIM_DEV_SYS_DEL=/sys/bus/netdevsim/del_device
NSIM_DEV_SYS_LINK=/sys/bus/netdevsim/link_device
NSIM_DEV_SYS_UNLINK=/sys/bus/netdevsim/unlink_device

socat_check()
{
	if [ ! -x "$(command -v socat)" ]; then
		echo "socat command not found. Skipping test"
		return 1
	fi

	return 0
}

setup_ns()
{
	set -e
	ip netns add nssv
	ip netns add nscl

	NSIM_DEV_1_NAME=$(find $NSIM_DEV_1_SYS/net -maxdepth 1 -type d ! \
		-path $NSIM_DEV_1_SYS/net -exec basename {} \;)
	NSIM_DEV_2_NAME=$(find $NSIM_DEV_2_SYS/net -maxdepth 1 -type d ! \
		-path $NSIM_DEV_2_SYS/net -exec basename {} \;)

	ip link set $NSIM_DEV_1_NAME netns nssv
	ip link set $NSIM_DEV_2_NAME netns nscl

	ip netns exec nssv ip addr add '192.168.1.1/24' dev $NSIM_DEV_1_NAME
	ip netns exec nscl ip addr add '192.168.1.2/24' dev $NSIM_DEV_2_NAME

	ip netns exec nssv ip link set dev $NSIM_DEV_1_NAME up
	ip netns exec nscl ip link set dev $NSIM_DEV_2_NAME up
	set +e
}

cleanup_ns()
{
	ip netns del nscl
	ip netns del nssv
}

###
### Code start
###

socat_check || exit 4

modprobe netdevsim

# linking

echo $NSIM_DEV_1_ID > $NSIM_DEV_SYS_NEW
echo $NSIM_DEV_2_ID > $NSIM_DEV_SYS_NEW
udevadm settle

setup_ns

NSIM_DEV_1_FD=$((256 + RANDOM % 256))
exec {NSIM_DEV_1_FD}</var/run/netns/nssv
NSIM_DEV_1_IFIDX=$(ip netns exec nssv cat /sys/class/net/$NSIM_DEV_1_NAME/ifindex)

NSIM_DEV_2_FD=$((256 + RANDOM % 256))
exec {NSIM_DEV_2_FD}</var/run/netns/nscl
NSIM_DEV_2_IFIDX=$(ip netns exec nscl cat /sys/class/net/$NSIM_DEV_2_NAME/ifindex)

echo "$NSIM_DEV_1_FD:$NSIM_DEV_1_IFIDX $NSIM_DEV_2_FD:2000" > $NSIM_DEV_SYS_LINK 2>/dev/null
if [ $? -eq 0 ]; then
	echo "linking with non-existent netdevsim should fail"
	cleanup_ns
	exit 1
fi

echo "$NSIM_DEV_1_FD:$NSIM_DEV_1_IFIDX 2000:$NSIM_DEV_2_IFIDX" > $NSIM_DEV_SYS_LINK 2>/dev/null
if [ $? -eq 0 ]; then
	echo "linking with non-existent netnsid should fail"
	cleanup_ns
	exit 1
fi

echo "$NSIM_DEV_1_FD:$NSIM_DEV_1_IFIDX $NSIM_DEV_1_FD:$NSIM_DEV_1_IFIDX" > $NSIM_DEV_SYS_LINK 2>/dev/null
if [ $? -eq 0 ]; then
	echo "linking with self should fail"
	cleanup_ns
	exit 1
fi

echo "$NSIM_DEV_1_FD:$NSIM_DEV_1_IFIDX $NSIM_DEV_2_FD:$NSIM_DEV_2_IFIDX" > $NSIM_DEV_SYS_LINK
if [ $? -ne 0 ]; then
	echo "linking netdevsim1 with netdevsim2 should succeed"
	cleanup_ns
	exit 1
fi

# argument error checking

echo "$NSIM_DEV_1_FD:$NSIM_DEV_1_IFIDX $NSIM_DEV_2_FD:a" > $NSIM_DEV_SYS_LINK 2>/dev/null
if [ $? -eq 0 ]; then
	echo "invalid arg should fail"
	cleanup_ns
	exit 1
fi

# send/recv packets

tmp_file=$(mktemp)
ip netns exec nssv socat TCP-LISTEN:1234,fork $tmp_file &
pid=$!
res=0

wait_local_port_listen nssv 1234 tcp

echo "HI" | ip netns exec nscl socat STDIN TCP:192.168.1.1:1234

count=$(cat $tmp_file | wc -c)
if [[ $count -ne 3 ]]; then
	echo "expected 3 bytes, got $count"
	res=1
fi

echo "$NSIM_DEV_1_FD:$NSIM_DEV_1_IFIDX" > $NSIM_DEV_SYS_UNLINK

echo $NSIM_DEV_2_ID > $NSIM_DEV_SYS_DEL

kill $pid
echo $NSIM_DEV_1_ID > $NSIM_DEV_SYS_DEL

cleanup_ns

modprobe -r netdevsim

exit $res
