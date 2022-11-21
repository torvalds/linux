#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only

source ethtool-common.sh

function get_value {
    local query="${SETTINGS_MAP[$1]}"

    echo $(ethtool -g $NSIM_NETDEV | \
        tail -n +$CURR_SETT_LINE | \
        awk -F':' -v pattern="$query:" '$0 ~ pattern {gsub(/[\t ]/, "", $2); print $2}')
}

function update_current_settings {
    for key in ${!SETTINGS_MAP[@]}; do
        CURRENT_SETTINGS[$key]=$(get_value $key)
    done
    echo ${CURRENT_SETTINGS[@]}
}

if ! ethtool -h | grep -q set-ring >/dev/null; then
    echo "SKIP: No --set-ring support in ethtool"
    exit 4
fi

NSIM_NETDEV=$(make_netdev)

set -o pipefail

declare -A SETTINGS_MAP=(
    ["rx"]="RX"
    ["rx-mini"]="RX Mini"
    ["rx-jumbo"]="RX Jumbo"
    ["tx"]="TX"
)

declare -A EXPECTED_SETTINGS=(
    ["rx"]=""
    ["rx-mini"]=""
    ["rx-jumbo"]=""
    ["tx"]=""
)

declare -A CURRENT_SETTINGS=(
    ["rx"]=""
    ["rx-mini"]=""
    ["rx-jumbo"]=""
    ["tx"]=""
)

MAX_VALUE=$((RANDOM % $((2**32-1))))
RING_MAX_LIST=$(ls $NSIM_DEV_DFS/ethtool/ring/)

for ring_max_entry in $RING_MAX_LIST; do
    echo $MAX_VALUE > $NSIM_DEV_DFS/ethtool/ring/$ring_max_entry
done

CURR_SETT_LINE=$(ethtool -g $NSIM_NETDEV | grep -i -m1 -n 'Current hardware settings' | cut -f1 -d:)

# populate the expected settings map
for key in ${!SETTINGS_MAP[@]}; do
    EXPECTED_SETTINGS[$key]=$(get_value $key)
done

# test
for key in ${!SETTINGS_MAP[@]}; do
    value=$((RANDOM % $MAX_VALUE))

    ethtool -G $NSIM_NETDEV "$key" "$value"

    EXPECTED_SETTINGS[$key]="$value"
    expected=${EXPECTED_SETTINGS[@]}
    current=$(update_current_settings)

    check $? "$current" "$expected"
    set +x
done

if [ $num_errors -eq 0 ]; then
    echo "PASSED all $((num_passes)) checks"
    exit 0
else
    echo "FAILED $num_errors/$((num_errors+num_passes)) checks"
    exit 1
fi
