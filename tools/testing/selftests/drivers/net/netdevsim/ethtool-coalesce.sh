#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only

source ethtool-common.sh

function get_value {
    local query="${SETTINGS_MAP[$1]}"

    echo $(ethtool -c $NSIM_NETDEV | \
        awk -F':' -v pattern="$query:" '$0 ~ pattern {gsub(/[ \t]/, "", $2); print $2}')
}

function update_current_settings {
    for key in ${!SETTINGS_MAP[@]}; do
        CURRENT_SETTINGS[$key]=$(get_value $key)
    done
    echo ${CURRENT_SETTINGS[@]}
}

if ! ethtool -h | grep -q coalesce; then
    echo "SKIP: No --coalesce support in ethtool"
    exit 4
fi

NSIM_NETDEV=$(make_netdev)

set -o pipefail

declare -A SETTINGS_MAP=(
    ["rx-frames-low"]="rx-frame-low"
    ["tx-frames-low"]="tx-frame-low"
    ["rx-frames-high"]="rx-frame-high"
    ["tx-frames-high"]="tx-frame-high"
    ["rx-usecs"]="rx-usecs"
    ["rx-frames"]="rx-frames"
    ["rx-usecs-irq"]="rx-usecs-irq"
    ["rx-frames-irq"]="rx-frames-irq"
    ["tx-usecs"]="tx-usecs"
    ["tx-frames"]="tx-frames"
    ["tx-usecs-irq"]="tx-usecs-irq"
    ["tx-frames-irq"]="tx-frames-irq"
    ["stats-block-usecs"]="stats-block-usecs"
    ["pkt-rate-low"]="pkt-rate-low"
    ["rx-usecs-low"]="rx-usecs-low"
    ["tx-usecs-low"]="tx-usecs-low"
    ["pkt-rate-high"]="pkt-rate-high"
    ["rx-usecs-high"]="rx-usecs-high"
    ["tx-usecs-high"]="tx-usecs-high"
    ["sample-interval"]="sample-interval"
)

declare -A CURRENT_SETTINGS=(
    ["rx-frames-low"]=""
    ["tx-frames-low"]=""
    ["rx-frames-high"]=""
    ["tx-frames-high"]=""
    ["rx-usecs"]=""
    ["rx-frames"]=""
    ["rx-usecs-irq"]=""
    ["rx-frames-irq"]=""
    ["tx-usecs"]=""
    ["tx-frames"]=""
    ["tx-usecs-irq"]=""
    ["tx-frames-irq"]=""
    ["stats-block-usecs"]=""
    ["pkt-rate-low"]=""
    ["rx-usecs-low"]=""
    ["tx-usecs-low"]=""
    ["pkt-rate-high"]=""
    ["rx-usecs-high"]=""
    ["tx-usecs-high"]=""
    ["sample-interval"]=""
)

declare -A EXPECTED_SETTINGS=(
    ["rx-frames-low"]=""
    ["tx-frames-low"]=""
    ["rx-frames-high"]=""
    ["tx-frames-high"]=""
    ["rx-usecs"]=""
    ["rx-frames"]=""
    ["rx-usecs-irq"]=""
    ["rx-frames-irq"]=""
    ["tx-usecs"]=""
    ["tx-frames"]=""
    ["tx-usecs-irq"]=""
    ["tx-frames-irq"]=""
    ["stats-block-usecs"]=""
    ["pkt-rate-low"]=""
    ["rx-usecs-low"]=""
    ["tx-usecs-low"]=""
    ["pkt-rate-high"]=""
    ["rx-usecs-high"]=""
    ["tx-usecs-high"]=""
    ["sample-interval"]=""
)

# populate the expected settings map
for key in ${!SETTINGS_MAP[@]}; do
    EXPECTED_SETTINGS[$key]=$(get_value $key)
done

# test
for key in ${!SETTINGS_MAP[@]}; do
    value=$((RANDOM % $((2**32-1))))

    ethtool -C $NSIM_NETDEV "$key" "$value"

    EXPECTED_SETTINGS[$key]="$value"
    expected=${EXPECTED_SETTINGS[@]}
    current=$(update_current_settings)

    check $? "$current" "$expected"
    set +x
done

# bool settings which ethtool displays on the same line
ethtool -C $NSIM_NETDEV adaptive-rx on
s=$(ethtool -c $NSIM_NETDEV | grep -q "Adaptive RX: on  TX: off")
check $? "$s" ""

ethtool -C $NSIM_NETDEV adaptive-tx on
s=$(ethtool -c $NSIM_NETDEV | grep -q "Adaptive RX: on  TX: on")
check $? "$s" ""

if [ $num_errors -eq 0 ]; then
    echo "PASSED all $((num_passes)) checks"
    exit 0
else
    echo "FAILED $num_errors/$((num_errors+num_passes)) checks"
    exit 1
fi
