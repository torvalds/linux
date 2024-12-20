#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only

source ethtool-common.sh

NSIM_NETDEV=$(make_netdev)
MACSEC_NETDEV=macsec_nsim

set -o pipefail

if ! ethtool -k $NSIM_NETDEV | grep -q 'macsec-hw-offload: on'; then
    echo "SKIP: netdevsim doesn't support MACsec offload"
    exit 4
fi

if ! ip link add link $NSIM_NETDEV $MACSEC_NETDEV type macsec offload mac 2>/dev/null; then
    echo "SKIP: couldn't create macsec device"
    exit 4
fi
ip link del $MACSEC_NETDEV

#
# test macsec offload API
#

ip link add link $NSIM_NETDEV "${MACSEC_NETDEV}" type macsec port 4 offload mac
check $?

ip link add link $NSIM_NETDEV "${MACSEC_NETDEV}2" type macsec address "aa:bb:cc:dd:ee:ff" port 5 offload mac
check $?

ip link add link $NSIM_NETDEV "${MACSEC_NETDEV}3" type macsec sci abbacdde01020304 offload mac
check $?

ip link add link $NSIM_NETDEV "${MACSEC_NETDEV}4" type macsec port 8 offload mac 2> /dev/null
check $? '' '' 1

ip macsec add "${MACSEC_NETDEV}" tx sa 0 pn 1024 on key 01 12345678901234567890123456789012
check $?

ip macsec add "${MACSEC_NETDEV}" rx port 1234 address "1c:ed:de:ad:be:ef"
check $?

ip macsec add "${MACSEC_NETDEV}" rx port 1234 address "1c:ed:de:ad:be:ef" sa 0 pn 1 on \
    key 00 0123456789abcdef0123456789abcdef
check $?

ip macsec add "${MACSEC_NETDEV}" rx port 1235 address "1c:ed:de:ad:be:ef" 2> /dev/null
check $? '' '' 1

# can't disable macsec offload when SAs are configured
ip link set "${MACSEC_NETDEV}" type macsec offload off 2> /dev/null
check $? '' '' 1

ip macsec offload "${MACSEC_NETDEV}" off 2> /dev/null
check $? '' '' 1

# toggle macsec offload via rtnetlink
ip link set "${MACSEC_NETDEV}2" type macsec offload off
check $?

ip link set "${MACSEC_NETDEV}2" type macsec offload mac
check $?

# toggle macsec offload via genetlink
ip macsec offload "${MACSEC_NETDEV}2" off
check $?

ip macsec offload "${MACSEC_NETDEV}2" mac
check $?

for dev in ${MACSEC_NETDEV}{,2,3} ; do
    ip link del $dev
    check $?
done


#
# test ethtool features when toggling offload
#

ip link add link $NSIM_NETDEV $MACSEC_NETDEV type macsec offload mac
TMP_FEATS_ON_1="$(ethtool -k $MACSEC_NETDEV)"

ip link set $MACSEC_NETDEV type macsec offload off
TMP_FEATS_OFF_1="$(ethtool -k $MACSEC_NETDEV)"

ip link set $MACSEC_NETDEV type macsec offload mac
TMP_FEATS_ON_2="$(ethtool -k $MACSEC_NETDEV)"

[ "$TMP_FEATS_ON_1" = "$TMP_FEATS_ON_2" ]
check $?

ip link del $MACSEC_NETDEV

ip link add link $NSIM_NETDEV $MACSEC_NETDEV type macsec
check $?

TMP_FEATS_OFF_2="$(ethtool -k $MACSEC_NETDEV)"
[ "$TMP_FEATS_OFF_1" = "$TMP_FEATS_OFF_2" ]
check $?

ip link set $MACSEC_NETDEV type macsec offload mac
check $?

TMP_FEATS_ON_3="$(ethtool -k $MACSEC_NETDEV)"
[ "$TMP_FEATS_ON_1" = "$TMP_FEATS_ON_3" ]
check $?


if [ $num_errors -eq 0 ]; then
    echo "PASSED all $((num_passes)) checks"
    exit 0
else
    echo "FAILED $num_errors/$((num_errors+num_passes)) checks"
    exit 1
fi
