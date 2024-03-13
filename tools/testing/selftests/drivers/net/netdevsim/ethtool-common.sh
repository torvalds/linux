#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only

NSIM_ID=$((RANDOM % 1024))
NSIM_DEV_SYS=/sys/bus/netdevsim/devices/netdevsim$NSIM_ID
NSIM_DEV_DFS=/sys/kernel/debug/netdevsim/netdevsim$NSIM_ID/ports/0
NSIM_NETDEV=
num_passes=0
num_errors=0

function cleanup_nsim {
    if [ -e $NSIM_DEV_SYS ]; then
	echo $NSIM_ID > /sys/bus/netdevsim/del_device
    fi
}

function cleanup {
    cleanup_nsim
}

trap cleanup EXIT

function check {
    local code=$1
    local str=$2
    local exp_str=$3
    local exp_fail=$4

    [ -z "$exp_fail" ] && cop="-ne" || cop="-eq"

    if [ $code $cop 0 ]; then
	((num_errors++))
	return
    fi

    if [ "$str" != "$exp_str"  ]; then
	echo -e "Expected: '$exp_str', got '$str'"
	((num_errors++))
	return
    fi

    ((num_passes++))
}

function make_netdev {
    # Make a netdevsim
    old_netdevs=$(ls /sys/class/net)

    if ! $(lsmod | grep -q netdevsim); then
	modprobe netdevsim
    fi

    echo $NSIM_ID $@ > /sys/bus/netdevsim/new_device
    # get new device name
    ls /sys/bus/netdevsim/devices/netdevsim${NSIM_ID}/net/
}
