#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
source net_helper.sh

NSIM_SV_ID=$((256 + RANDOM % 256))
NSIM_SV_SYS=/sys/bus/netdevsim/devices/netdevsim$NSIM_SV_ID
NSIM_CL_ID=$((512 + RANDOM % 256))
NSIM_CL_SYS=/sys/bus/netdevsim/devices/netdevsim$NSIM_CL_ID

NSIM_DEV_SYS_NEW=/sys/bus/netdevsim/new_device
NSIM_DEV_SYS_DEL=/sys/bus/netdevsim/del_device
NSIM_DEV_SYS_LINK=/sys/bus/netdevsim/link_device
NSIM_DEV_SYS_UNLINK=/sys/bus/netdevsim/unlink_device

SERVER_IP=192.168.1.1
CLIENT_IP=192.168.1.2
SERVER_PORT=48675

# busy poll config
MAX_EVENTS=8
BUSY_POLL_USECS=0
BUSY_POLL_BUDGET=16
PREFER_BUSY_POLL=1

# IRQ deferral config
NAPI_DEFER_HARD_IRQS=100
GRO_FLUSH_TIMEOUT=50000
SUSPEND_TIMEOUT=20000000

setup_ns()
{
	set -e
	ip netns add nssv
	ip netns add nscl

	NSIM_SV_NAME=$(find $NSIM_SV_SYS/net -maxdepth 1 -type d ! \
		-path $NSIM_SV_SYS/net -exec basename {} \;)
	NSIM_CL_NAME=$(find $NSIM_CL_SYS/net -maxdepth 1 -type d ! \
		-path $NSIM_CL_SYS/net -exec basename {} \;)

	# ensure the server has 1 queue
	ethtool -L $NSIM_SV_NAME combined 1 2>/dev/null

	ip link set $NSIM_SV_NAME netns nssv
	ip link set $NSIM_CL_NAME netns nscl

	ip netns exec nssv ip addr add "${SERVER_IP}/24" dev $NSIM_SV_NAME
	ip netns exec nscl ip addr add "${CLIENT_IP}/24" dev $NSIM_CL_NAME

	ip netns exec nssv ip link set dev $NSIM_SV_NAME up
	ip netns exec nscl ip link set dev $NSIM_CL_NAME up

	set +e
}

cleanup_ns()
{
	ip netns del nscl
	ip netns del nssv
}

test_busypoll()
{
	suspend_value=${1:-0}
	tmp_file=$(mktemp)
	out_file=$(mktemp)

	# fill a test file with random data
	dd if=/dev/urandom of=${tmp_file} bs=1M count=1 2> /dev/null

	timeout -k 1s 30s ip netns exec nssv ./busy_poller         \
					     -p${SERVER_PORT}      \
					     -b${SERVER_IP}        \
					     -m${MAX_EVENTS}       \
					     -u${BUSY_POLL_USECS}  \
					     -P${PREFER_BUSY_POLL} \
					     -g${BUSY_POLL_BUDGET} \
					     -i${NSIM_SV_IFIDX}    \
					     -s${suspend_value}    \
					     -o${out_file}&

	wait_local_port_listen nssv ${SERVER_PORT} tcp

	ip netns exec nscl socat -u $tmp_file TCP:${SERVER_IP}:${SERVER_PORT}

	wait

	tmp_file_md5sum=$(md5sum $tmp_file | cut -f1 -d' ')
	out_file_md5sum=$(md5sum $out_file | cut -f1 -d' ')

	if [ "$tmp_file_md5sum" = "$out_file_md5sum" ]; then
		res=0
	else
		echo "md5sum mismatch"
		echo "input file md5sum: ${tmp_file_md5sum}";
		echo "output file md5sum: ${out_file_md5sum}";
		res=1
	fi

	rm $out_file $tmp_file

	return $res
}

test_busypoll_with_suspend()
{
	test_busypoll ${SUSPEND_TIMEOUT}

	return $?
}

###
### Code start
###

modprobe netdevsim

# linking

echo $NSIM_SV_ID > $NSIM_DEV_SYS_NEW
echo $NSIM_CL_ID > $NSIM_DEV_SYS_NEW
udevadm settle

setup_ns

NSIM_SV_FD=$((256 + RANDOM % 256))
exec {NSIM_SV_FD}</var/run/netns/nssv
NSIM_SV_IFIDX=$(ip netns exec nssv cat /sys/class/net/$NSIM_SV_NAME/ifindex)

NSIM_CL_FD=$((256 + RANDOM % 256))
exec {NSIM_CL_FD}</var/run/netns/nscl
NSIM_CL_IFIDX=$(ip netns exec nscl cat /sys/class/net/$NSIM_CL_NAME/ifindex)

echo "$NSIM_SV_FD:$NSIM_SV_IFIDX $NSIM_CL_FD:$NSIM_CL_IFIDX" > \
     $NSIM_DEV_SYS_LINK

if [ $? -ne 0 ]; then
	echo "linking netdevsim1 with netdevsim2 should succeed"
	cleanup_ns
	exit 1
fi

test_busypoll
if [ $? -ne 0 ]; then
	echo "test_busypoll failed"
	cleanup_ns
	exit 1
fi

test_busypoll_with_suspend
if [ $? -ne 0 ]; then
	echo "test_busypoll_with_suspend failed"
	cleanup_ns
	exit 1
fi

echo "$NSIM_SV_FD:$NSIM_SV_IFIDX" > $NSIM_DEV_SYS_UNLINK

echo $NSIM_CL_ID > $NSIM_DEV_SYS_DEL

cleanup_ns

modprobe -r netdevsim

exit 0
