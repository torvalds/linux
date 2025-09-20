#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only

source ethtool-common.sh

# Bail if ethtool is too old
if ! ethtool -h | grep include-stat 2>&1 >/dev/null; then
    echo "SKIP: No --include-statistics support in ethtool"
    exit 4
fi

NSIM_NETDEV=$(make_netdev)

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
