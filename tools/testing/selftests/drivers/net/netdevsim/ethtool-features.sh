#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only

source ethtool-common.sh

NSIM_NETDEV=$(make_netdev)

set -o pipefail

FEATS="
  tx-checksum-ip-generic
  tx-scatter-gather
  tx-tcp-segmentation
  generic-segmentation-offload
  generic-receive-offload"

for feat in $FEATS ; do
    s=$(ethtool --json -k $NSIM_NETDEV | jq ".[].\"$feat\".active" 2>/dev/null)
    check $? "$s" true

    s=$(ethtool --json -k $NSIM_NETDEV | jq ".[].\"$feat\".fixed" 2>/dev/null)
    check $? "$s" false
done

if [ $num_errors -eq 0 ]; then
    echo "PASSED all $((num_passes)) checks"
    exit 0
else
    echo "FAILED $num_errors/$((num_errors+num_passes)) checks"
    exit 1
fi
