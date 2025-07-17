#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

set -e

NSIM_LRO_ID=$((256 + RANDOM % 256))
NSIM_LRO_SYS=/sys/bus/netdevsim/devices/netdevsim$NSIM_LRO_ID

NSIM_DEV_SYS_NEW=/sys/bus/netdevsim/new_device
NSIM_DEV_SYS_DEL=/sys/bus/netdevsim/del_device

cleanup()
{
	set +e
	ip link del dummyteam &>/dev/null
	ip link del team0 &>/dev/null
	echo $NSIM_LRO_ID > $NSIM_DEV_SYS_DEL
	modprobe -r netdevsim
}

# Trigger LRO propagation to the lower.
# https://lore.kernel.org/netdev/aBvOpkIoxcr9PfDg@mini-arch/
team_lro()
{
	# using netdevsim because it supports NETIF_F_LRO
	NSIM_LRO_NAME=$(find $NSIM_LRO_SYS/net -maxdepth 1 -type d ! \
		-path $NSIM_LRO_SYS/net -exec basename {} \;)

	ip link add name team0 type team
	ip link set $NSIM_LRO_NAME down
	ip link set dev $NSIM_LRO_NAME master team0
	ip link set team0 up
	ethtool -K team0 large-receive-offload off

	ip link del team0
}

# Trigger promisc propagation to the lower during IFLA_MASTER.
# https://lore.kernel.org/netdev/20250506032328.3003050-1-sdf@fomichev.me/
team_promisc()
{
	ip link add name dummyteam type dummy
	ip link add name team0 type team
	ip link set dummyteam down
	ip link set team0 promisc on
	ip link set dev dummyteam master team0
	ip link set team0 up

	ip link del team0
	ip link del dummyteam
}

# Trigger promisc propagation to the lower via netif_change_flags (aka
# ndo_change_rx_flags).
# https://lore.kernel.org/netdev/20250514220319.3505158-1-stfomichev@gmail.com/
team_change_flags()
{
	ip link add name dummyteam type dummy
	ip link add name team0 type team
	ip link set dummyteam down
	ip link set dev dummyteam master team0
	ip link set team0 up
	ip link set team0 promisc on

	# Make sure we can add more L2 addresses without any issues.
	ip link add link team0 address 00:00:00:00:00:01 team0.1 type macvlan
	ip link set team0.1 up

	ip link del team0.1
	ip link del team0
	ip link del dummyteam
}

trap cleanup EXIT
modprobe netdevsim || :
echo $NSIM_LRO_ID > $NSIM_DEV_SYS_NEW
udevadm settle
team_lro
team_promisc
team_change_flags
