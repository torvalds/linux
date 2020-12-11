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

function get_netdev_name {
    local -n old=$1

    new=$(ls /sys/class/net)

    for netdev in $new; do
	for check in $old; do
            [ $netdev == $check ] && break
	done

	if [ $netdev != $check ]; then
	    echo $netdev
	    break
	fi
    done
}

function check {
    local code=$1
    local str=$2
    local exp_str=$3

    if [ $code -ne 0 ]; then
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

# Bail if ethtool is too old
if ! ethtool -h | grep include-stat 2>&1 >/dev/null; then
    echo "SKIP: No --include-statistics support in ethtool"
    exit 4
fi

# Make a netdevsim
old_netdevs=$(ls /sys/class/net)

modprobe netdevsim
echo $NSIM_ID > /sys/bus/netdevsim/new_device

NSIM_NETDEV=`get_netdev_name old_netdevs`

set -o pipefail

echo n > $NSIM_DEV_DFS/ethtool/pause/report_stats_tx
echo n > $NSIM_DEV_DFS/ethtool/pause/report_stats_rx

s=$(ethtool --json -a $NSIM_NETDEV | jq '.[].statistics')
check $? "$s" "null"

s=$(ethtool -I --json -a $NSIM_NETDEV | jq '.[].statistics')
check $? "$s" "{}"

echo y > $NSIM_DEV_DFS/ethtool/pause/report_stats_tx

s=$(ethtool -I --json -a $NSIM_NETDEV | jq '.[].statistics | length')
check $? "$s" "1"

s=$(ethtool -I --json -a $NSIM_NETDEV | jq '.[].statistics.tx_pause_frames')
check $? "$s" "2"

echo y > $NSIM_DEV_DFS/ethtool/pause/report_stats_rx

s=$(ethtool -I --json -a $NSIM_NETDEV | jq '.[].statistics | length')
check $? "$s" "2"

s=$(ethtool -I --json -a $NSIM_NETDEV | jq '.[].statistics.rx_pause_frames')
check $? "$s" "1"
s=$(ethtool -I --json -a $NSIM_NETDEV | jq '.[].statistics.tx_pause_frames')
check $? "$s" "2"

if [ $num_errors -eq 0 ]; then
    echo "PASSED all $((num_passes)) checks"
    exit 0
else
    echo "FAILED $num_errors/$((num_errors+num_passes)) checks"
    exit 1
fi
