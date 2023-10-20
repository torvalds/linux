#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

set -o pipefail

NS=netns-name-test
DEV=dummy-dev0
DEV2=dummy-dev1
ALT_NAME=some-alt-name

RET_CODE=0

cleanup() {
    ip netns del $NS
}

trap cleanup EXIT

fail() {
    echo "ERROR: ${1:-unexpected return code} (ret: $_)" >&2
    RET_CODE=1
}

ip netns add $NS

#
# Test basic move without a rename
#
ip -netns $NS link add name $DEV type dummy || fail
ip -netns $NS link set dev $DEV netns 1 ||
    fail "Can't perform a netns move"
ip link show dev $DEV >> /dev/null || fail "Device not found after move"
ip link del $DEV || fail

#
# Test move with a conflict
#
ip link add name $DEV type dummy
ip -netns $NS link add name $DEV type dummy || fail
ip -netns $NS link set dev $DEV netns 1 2> /dev/null &&
    fail "Performed a netns move with a name conflict"
ip link show dev $DEV >> /dev/null || fail "Device not found after move"
ip -netns $NS link del $DEV || fail
ip link del $DEV || fail

#
# Test move with a conflict and rename
#
ip link add name $DEV type dummy
ip -netns $NS link add name $DEV type dummy || fail
ip -netns $NS link set dev $DEV netns 1 name $DEV2 ||
    fail "Can't perform a netns move with rename"
ip link del $DEV2 || fail
ip link del $DEV || fail

#
# Test dup alt-name with netns move
#
ip link add name $DEV type dummy || fail
ip link property add dev $DEV altname $ALT_NAME || fail
ip -netns $NS link add name $DEV2 type dummy || fail
ip -netns $NS link property add dev $DEV2 altname $ALT_NAME || fail

ip -netns $NS link set dev $DEV2 netns 1 2> /dev/null &&
    fail "Moved with alt-name dup"

ip link del $DEV || fail
ip -netns $NS link del $DEV2 || fail

#
# Test creating alt-name in one net-ns and using in another
#
ip -netns $NS link add name $DEV type dummy || fail
ip -netns $NS link property add dev $DEV altname $ALT_NAME || fail
ip -netns $NS link set dev $DEV netns 1 || fail
ip link show dev $ALT_NAME >> /dev/null || fail "Can't find alt-name after move"
ip  -netns $NS link show dev $ALT_NAME 2> /dev/null &&
    fail "Can still find alt-name after move"
ip link del $DEV || fail

echo -ne "$(basename $0) \t\t\t\t"
if [ $RET_CODE -eq 0 ]; then
    echo "[  OK  ]"
else
    echo "[ FAIL ]"
fi
exit $RET_CODE
