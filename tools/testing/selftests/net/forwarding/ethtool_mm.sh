#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

ALL_TESTS="
	manual_with_verification_h1_to_h2
	manual_with_verification_h2_to_h1
	manual_without_verification_h1_to_h2
	manual_without_verification_h2_to_h1
	manual_failed_verification_h1_to_h2
	manual_failed_verification_h2_to_h1
	lldp
"

NUM_NETIFS=2
REQUIRE_MZ=no
PREEMPTIBLE_PRIO=0
source lib.sh

traffic_test()
{
	local if=$1; shift
	local src=$1; shift
	local num_pkts=10000
	local before=
	local after=
	local delta=

	before=$(ethtool_std_stats_get $if "eth-mac" "FramesTransmittedOK" $src)

	$MZ $if -q -c $num_pkts -p 64 -b bcast -t ip -R $PREEMPTIBLE_PRIO

	after=$(ethtool_std_stats_get $if "eth-mac" "FramesTransmittedOK" $src)

	delta=$((after - before))

	# Allow an extra 1% tolerance for random packets sent by the stack
	[ $delta -ge $num_pkts ] && [ $delta -le $((num_pkts + 100)) ]
}

manual_with_verification()
{
	local tx=$1; shift
	local rx=$1; shift

	RET=0

	# It isn't completely clear from IEEE 802.3-2018 Figure 99-5: Transmit
	# Processing state diagram whether the "send_r" variable (send response
	# to verification frame) should be taken into consideration while the
	# MAC Merge TX direction is disabled. That being said, at least the
	# NXP ENETC does not, and requires tx-enabled on in order to respond to
	# the link partner's verification frames.
	ethtool --set-mm $rx tx-enabled on
	ethtool --set-mm $tx verify-enabled on tx-enabled on

	# Wait for verification to finish
	sleep 1

	ethtool --json --show-mm $tx | jq -r '.[]."verify-status"' | \
		grep -q 'SUCCEEDED'
	check_err "$?" "Verification did not succeed"

	ethtool --json --show-mm $tx | jq -r '.[]."tx-active"' | grep -q 'true'
	check_err "$?" "pMAC TX is not active"

	traffic_test $tx "pmac"
	check_err "$?" "Traffic did not get sent through $tx's pMAC"

	ethtool --set-mm $tx verify-enabled off tx-enabled off
	ethtool --set-mm $rx tx-enabled off

	log_test "Manual configuration with verification: $tx to $rx"
}

manual_with_verification_h1_to_h2()
{
	manual_with_verification $h1 $h2
}

manual_with_verification_h2_to_h1()
{
	manual_with_verification $h2 $h1
}

manual_without_verification()
{
	local tx=$1; shift
	local rx=$1; shift

	RET=0

	ethtool --set-mm $tx verify-enabled off tx-enabled on

	ethtool --json --show-mm $tx | jq -r '.[]."verify-status"' | \
		grep -q 'DISABLED'
	check_err "$?" "Verification is not disabled"

	ethtool --json --show-mm $tx | jq -r '.[]."tx-active"' | grep -q 'true'
	check_err "$?" "pMAC TX is not active"

	traffic_test $tx "pmac"
	check_err "$?" "Traffic did not get sent through $tx's pMAC"

	ethtool --set-mm $tx verify-enabled off tx-enabled off

	log_test "Manual configuration without verification: $tx to $rx"
}

manual_without_verification_h1_to_h2()
{
	manual_without_verification $h1 $h2
}

manual_without_verification_h2_to_h1()
{
	manual_without_verification $h2 $h1
}

manual_failed_verification()
{
	local tx=$1; shift
	local rx=$1; shift

	RET=0

	ethtool --set-mm $rx pmac-enabled off
	ethtool --set-mm $tx verify-enabled on tx-enabled on

	# Wait for verification to time out
	sleep 1

	ethtool --json --show-mm $tx | jq -r '.[]."verify-status"' | \
		grep -q 'SUCCEEDED'
	check_fail "$?" "Verification succeeded when it shouldn't have"

	ethtool --json --show-mm $tx | jq -r '.[]."tx-active"' | grep -q 'true'
	check_fail "$?" "pMAC TX is active when it shouldn't have"

	traffic_test $tx "emac"
	check_err "$?" "Traffic did not get sent through $tx's eMAC"

	ethtool --set-mm $tx verify-enabled off tx-enabled off
	ethtool --set-mm $rx pmac-enabled on

	log_test "Manual configuration with failed verification: $tx to $rx"
}

manual_failed_verification_h1_to_h2()
{
	manual_failed_verification $h1 $h2
}

manual_failed_verification_h2_to_h1()
{
	manual_failed_verification $h2 $h1
}

lldp_change_add_frag_size()
{
	local add_frag_size=$1

	lldptool -T -i $h1 -V addEthCaps addFragSize=$add_frag_size >/dev/null
	# Wait for TLVs to be received
	sleep 2
	lldptool -i $h2 -t -n -V addEthCaps | \
		grep -q "Additional fragment size: $add_frag_size"
}

lldp()
{
	RET=0

	systemctl start lldpad

	# Configure the interfaces to receive and transmit LLDPDUs
	lldptool -L -i $h1 adminStatus=rxtx >/dev/null
	lldptool -L -i $h2 adminStatus=rxtx >/dev/null

	# Enable the transmission of Additional Ethernet Capabilities TLV
	lldptool -T -i $h1 -V addEthCaps enableTx=yes >/dev/null
	lldptool -T -i $h2 -V addEthCaps enableTx=yes >/dev/null

	# Wait for TLVs to be received
	sleep 2

	lldptool -i $h1 -t -n -V addEthCaps | \
		grep -q "Preemption capability active"
	check_err "$?" "$h1 pMAC TX is not active"

	lldptool -i $h2 -t -n -V addEthCaps | \
		grep -q "Preemption capability active"
	check_err "$?" "$h2 pMAC TX is not active"

	lldp_change_add_frag_size 3
	check_err "$?" "addFragSize 3"

	lldp_change_add_frag_size 2
	check_err "$?" "addFragSize 2"

	lldp_change_add_frag_size 1
	check_err "$?" "addFragSize 1"

	lldp_change_add_frag_size 0
	check_err "$?" "addFragSize 0"

	traffic_test $h1 "pmac"
	check_err "$?" "Traffic did not get sent through $h1's pMAC"

	traffic_test $h2 "pmac"
	check_err "$?" "Traffic did not get sent through $h2's pMAC"

	systemctl stop lldpad

	log_test "LLDP"
}

h1_create()
{
	ip link set dev $h1 up

	tc qdisc add dev $h1 root mqprio num_tc 4 map 0 1 2 3 \
		queues 1@0 1@1 1@2 1@3 \
		fp P E E E \
		hw 1

	ethtool --set-mm $h1 pmac-enabled on tx-enabled off verify-enabled off
}

h2_create()
{
	ip link set dev $h2 up

	ethtool --set-mm $h2 pmac-enabled on tx-enabled off verify-enabled off

	tc qdisc add dev $h2 root mqprio num_tc 4 map 0 1 2 3 \
		queues 1@0 1@1 1@2 1@3 \
		fp P E E E \
		hw 1
}

h1_destroy()
{
	ethtool --set-mm $h1 pmac-enabled off tx-enabled off verify-enabled off

	tc qdisc del dev $h1 root

	ip link set dev $h1 down
}

h2_destroy()
{
	tc qdisc del dev $h2 root

	ethtool --set-mm $h2 pmac-enabled off tx-enabled off verify-enabled off

	ip link set dev $h2 down
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	h2=${NETIFS[p2]}

	h1_create
	h2_create
}

cleanup()
{
	pre_cleanup

	h2_destroy
	h1_destroy
}

check_ethtool_mm_support
check_tc_fp_support
require_command lldptool
bail_on_lldpad "autoconfigure the MAC Merge layer" "configure it manually"

for netif in ${NETIFS[@]}; do
	ethtool --show-mm $netif 2>&1 &> /dev/null
	if [[ $? -ne 0 ]]; then
		echo "SKIP: $netif does not support MAC Merge"
		exit $ksft_skip
	fi
done

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS
