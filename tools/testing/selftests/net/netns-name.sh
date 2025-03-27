#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

source lib.sh
set -o pipefail

DEV=dummy-dev0
DEV2=dummy-dev1
ALT_NAME=some-alt-name

RET_CODE=0

cleanup() {
    cleanup_ns $NS $test_ns
}

trap cleanup EXIT

fail() {
    echo "ERROR: ${1:-unexpected return code} (ret: $_)" >&2
    RET_CODE=1
}

setup_ns NS test_ns

#
# Test basic move without a rename
#
ip -netns $NS link add name $DEV type dummy || fail
ip -netns $NS link set dev $DEV netns $test_ns ||
    fail "Can't perform a netns move"
ip -netns $test_ns link show dev $DEV >> /dev/null || fail "Device not found after move"
ip -netns $test_ns link del $DEV || fail

#
# Test move with a conflict
#
ip -netns $test_ns link add name $DEV type dummy
ip -netns $NS link add name $DEV type dummy || fail
ip -netns $NS link set dev $DEV netns $test_ns 2> /dev/null &&
    fail "Performed a netns move with a name conflict"
ip -netns $test_ns link show dev $DEV >> /dev/null || fail "Device not found after move"
ip -netns $NS link del $DEV || fail
ip -netns $test_ns link del $DEV || fail

#
# Test move with a conflict and rename
#
ip -netns $test_ns link add name $DEV type dummy
ip -netns $NS link add name $DEV type dummy || fail
ip -netns $NS link set dev $DEV netns $test_ns name $DEV2 ||
    fail "Can't perform a netns move with rename"
ip -netns $test_ns link del $DEV2 || fail
ip -netns $test_ns link del $DEV || fail

#
# Test dup alt-name with netns move
#
ip -netns $test_ns link add name $DEV type dummy || fail
ip -netns $test_ns link property add dev $DEV altname $ALT_NAME || fail
ip -netns $NS link add name $DEV2 type dummy || fail
ip -netns $NS link property add dev $DEV2 altname $ALT_NAME || fail

ip -netns $NS link set dev $DEV2 netns $test_ns 2> /dev/null &&
    fail "Moved with alt-name dup"

ip -netns $test_ns link del $DEV || fail
ip -netns $NS link del $DEV2 || fail

#
# Test creating alt-name in one net-ns and using in another
#
ip -netns $NS link add name $DEV type dummy || fail
ip -netns $NS link property add dev $DEV altname $ALT_NAME || fail
ip -netns $NS link set dev $DEV netns $test_ns || fail
ip -netns $test_ns link show dev $ALT_NAME >> /dev/null || fail "Can't find alt-name after move"
ip -netns $NS link show dev $ALT_NAME 2> /dev/null &&
    fail "Can still find alt-name after move"
ip -netns $test_ns link del $DEV || fail

#
# Test no conflict of the same name/ifindex in different netns
#
ip -netns $NS link add name $DEV index 100 type dummy || fail
ip -netns $NS link add netns $test_ns name $DEV index 100 type dummy ||
    fail "Can create in netns without moving"
ip -netns $test_ns link show dev $DEV >> /dev/null || fail "Device not found"
ip -netns $NS link del $DEV || fail
ip -netns $test_ns link del $DEV || fail

echo -ne "$(basename $0) \t\t\t\t"
if [ $RET_CODE -eq 0 ]; then
    echo "[  OK  ]"
else
    echo "[ FAIL ]"
fi
exit $RET_CODE
