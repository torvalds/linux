#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only

source ethtool-common.sh

NSIM_NETDEV=$(make_netdev)
[ a$ETHTOOL == a ] && ETHTOOL=ethtool

set -o pipefail

# Since commit 2b3ddcb35357 ("ethtool: fec: Change the prompt ...")
# in ethtool CLI the Configured lines start with Supported/Configured.
configured=$($ETHTOOL --show-fec $NSIM_NETDEV | tail -2 | head -1 | cut -d' ' -f1)

# netdevsim starts out with None/None
s=$($ETHTOOL --show-fec $NSIM_NETDEV | tail -2)
check $? "$s" "$configured FEC encodings: None
Active FEC encoding: None"

# Test Auto
$ETHTOOL --set-fec $NSIM_NETDEV encoding auto
check $?
s=$($ETHTOOL --show-fec $NSIM_NETDEV | tail -2)
check $? "$s" "$configured FEC encodings: Auto
Active FEC encoding: Off"

# Test case in-sensitivity
for o in off Off OFF; do
    $ETHTOOL --set-fec $NSIM_NETDEV encoding $o
    check $?
    s=$($ETHTOOL --show-fec $NSIM_NETDEV | tail -2)
    check $? "$s" "$configured FEC encodings: Off
Active FEC encoding: Off"
done

for o in BaseR baser BAser; do
    $ETHTOOL --set-fec $NSIM_NETDEV encoding $o
    check $?
    s=$($ETHTOOL --show-fec $NSIM_NETDEV | tail -2)
    check $? "$s" "$configured FEC encodings: BaseR
Active FEC encoding: BaseR"
done

for o in llrs rs; do
    $ETHTOOL --set-fec $NSIM_NETDEV encoding $o
    check $?
    s=$($ETHTOOL --show-fec $NSIM_NETDEV | tail -2)
    check $? "$s" "$configured FEC encodings: ${o^^}
Active FEC encoding: ${o^^}"
done

# Test mutliple bits
$ETHTOOL --set-fec $NSIM_NETDEV encoding rs llrs
check $?
s=$($ETHTOOL --show-fec $NSIM_NETDEV | tail -2)
check $? "$s" "$configured FEC encodings: RS LLRS
Active FEC encoding: LLRS"

$ETHTOOL --set-fec $NSIM_NETDEV encoding rs off auto
check $?
s=$($ETHTOOL --show-fec $NSIM_NETDEV | tail -2)
check $? "$s" "$configured FEC encodings: Auto Off RS
Active FEC encoding: RS"

# Make sure other link modes are rejected
$ETHTOOL --set-fec $NSIM_NETDEV encoding FIBRE 2>/dev/null
check $? '' '' 1

$ETHTOOL --set-fec $NSIM_NETDEV encoding bla-bla-bla 2>/dev/null
check $? '' '' 1

# Try JSON
$ETHTOOL --json --show-fec $NSIM_NETDEV | jq empty >>/dev/null 2>&1
if [ $? -eq 0 ]; then
    $ETHTOOL --set-fec $NSIM_NETDEV encoding auto
    check $?

    s=$($ETHTOOL --json --show-fec $NSIM_NETDEV | jq '.[].config[]')
    check $? "$s" '"Auto"'
    s=$($ETHTOOL --json --show-fec $NSIM_NETDEV | jq '.[].active[]')
    check $? "$s" '"Off"'

    $ETHTOOL --set-fec $NSIM_NETDEV encoding auto RS
    check $?

    s=$($ETHTOOL --json --show-fec $NSIM_NETDEV | jq '.[].config[]')
    check $? "$s" '"Auto"
"RS"'
    s=$($ETHTOOL --json --show-fec $NSIM_NETDEV | jq '.[].active[]')
    check $? "$s" '"RS"'
fi

# Test error injection
echo 11 > $NSIM_DEV_DFS/ethtool/get_err

$ETHTOOL --show-fec $NSIM_NETDEV >>/dev/null 2>&1
check $? '' '' 1

echo 0 > $NSIM_DEV_DFS/ethtool/get_err
echo 11 > $NSIM_DEV_DFS/ethtool/set_err

$ETHTOOL --show-fec $NSIM_NETDEV  >>/dev/null 2>&1
check $?

$ETHTOOL --set-fec $NSIM_NETDEV encoding RS 2>/dev/null
check $? '' '' 1

if [ $num_errors -eq 0 ]; then
    echo "PASSED all $((num_passes)) checks"
    exit 0
else
    echo "FAILED $num_errors/$((num_errors+num_passes)) checks"
    exit 1
fi
