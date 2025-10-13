#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
source lib.sh

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

setup_ns()
{
	set -e
	ip netns add nssv
	ip netns add nscl

	NSIM_SV_NAME=$(find $NSIM_SV_SYS/net -maxdepth 1 -type d ! \
		-path $NSIM_SV_SYS/net -exec basename {} \;)
	NSIM_CL_NAME=$(find $NSIM_CL_SYS/net -maxdepth 1 -type d ! \
		-path $NSIM_CL_SYS/net -exec basename {} \;)

	ip link set $NSIM_SV_NAME netns nssv
	ip link set $NSIM_CL_NAME netns nscl

	ip netns exec nssv ip addr add "${SERVER_IP}/24" dev $NSIM_SV_NAME
	ip netns exec nscl ip addr add "${CLIENT_IP}/24" dev $NSIM_CL_NAME

	ip netns exec nssv ip link set dev $NSIM_SV_NAME up
	ip netns exec nscl ip link set dev $NSIM_CL_NAME up

	# Enable passive TFO
	ip netns exec nssv sysctl -w net.ipv4.tcp_fastopen=519 > /dev/null

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

out_file=$(mktemp)

timeout -k 1s 30s ip netns exec nssv ./tfo        \
				-s                \
				-p ${SERVER_PORT} \
				-o ${out_file}&

wait_local_port_listen nssv ${SERVER_PORT} tcp

ip netns exec nscl ./tfo -c -h ${SERVER_IP} -p ${SERVER_PORT}

wait

res=$(cat $out_file)
rm $out_file

if [ "$res" = "0" ]; then
	echo "got invalid NAPI ID from passive TFO socket"
	cleanup_ns
	exit 1
fi

echo "$NSIM_SV_FD:$NSIM_SV_IFIDX" > $NSIM_DEV_SYS_UNLINK

echo $NSIM_CL_ID > $NSIM_DEV_SYS_DEL

cleanup_ns

modprobe -r netdevsim

exit 0
