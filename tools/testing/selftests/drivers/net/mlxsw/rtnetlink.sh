#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test various interface configuration scenarios. Observe that configurations
# deemed valid by mlxsw succeed, invalid configurations fail and that no traces
# are produced. To prevent the test from passing in case traces are produced,
# the user can set the 'kernel.panic_on_warn' and 'kernel.panic_on_oops'
# sysctls in its environment.

lib_dir=$(dirname $0)/../../../net/forwarding

ALL_TESTS="
	rif_set_addr_test
	rif_inherit_bridge_addr_test
	rif_non_inherit_bridge_addr_test
"
NUM_NETIFS=2
source $lib_dir/lib.sh

setup_prepare()
{
	swp1=${NETIFS[p1]}
	swp2=${NETIFS[p2]}

	ip link set dev $swp1 up
	ip link set dev $swp2 up
}

cleanup()
{
	pre_cleanup

	ip link set dev $swp2 down
	ip link set dev $swp1 down
}

rif_set_addr_test()
{
	local swp1_mac=$(mac_get $swp1)
	local swp2_mac=$(mac_get $swp2)

	RET=0

	# $swp1 and $swp2 likely got their IPv6 local addresses already, but
	# here we need to test the transition to RIF.
	ip addr flush dev $swp1
	ip addr flush dev $swp2
	sleep .1

	ip addr add dev $swp1 192.0.2.1/28
	check_err $?

	ip link set dev $swp1 addr 00:11:22:33:44:55
	check_err $?

	# IP address enablement should be rejected if the MAC address prefix
	# doesn't match other RIFs.
	ip addr add dev $swp2 192.0.2.2/28 &>/dev/null
	check_fail $? "IP address addition passed for a device with a wrong MAC"
	ip addr add dev $swp2 192.0.2.2/28 2>&1 >/dev/null \
	    | grep -q mlxsw_spectrum
	check_err $? "no extack for IP address addition"

	ip link set dev $swp2 addr 00:11:22:33:44:66
	check_err $?
	ip addr add dev $swp2 192.0.2.2/28 &>/dev/null
	check_err $?

	# Change of MAC address of a RIF should be forbidden if the new MAC
	# doesn't share the prefix with other MAC addresses.
	ip link set dev $swp2 addr 00:11:22:33:00:66 &>/dev/null
	check_fail $? "change of MAC address passed for a wrong MAC"
	ip link set dev $swp2 addr 00:11:22:33:00:66 2>&1 >/dev/null \
	    | grep -q mlxsw_spectrum
	check_err $? "no extack for MAC address change"

	log_test "RIF - bad MAC change"

	ip addr del dev $swp2 192.0.2.2/28
	ip addr del dev $swp1 192.0.2.1/28

	ip link set dev $swp2 addr $swp2_mac
	ip link set dev $swp1 addr $swp1_mac
}

rif_inherit_bridge_addr_test()
{
	RET=0

	# Create first RIF
	ip addr add dev $swp1 192.0.2.1/28
	check_err $?

	# Create a FID RIF
	ip link add name br1 up type bridge vlan_filtering 0
	ip link set dev $swp2 master br1
	ip addr add dev br1 192.0.2.17/28
	check_err $?

	# Prepare a device with a low MAC address
	ip link add name d up type dummy
	ip link set dev d addr 00:11:22:33:44:55

	# Attach the device to br1. That prompts bridge address change, which
	# should be vetoed, thus preventing the attachment.
	ip link set dev d master br1 &>/dev/null
	check_fail $? "Device with low MAC was permitted to attach a bridge with RIF"
	ip link set dev d master br1 2>&1 >/dev/null \
	    | grep -q mlxsw_spectrum
	check_err $? "no extack for bridge attach rejection"

	ip link set dev $swp2 addr 00:11:22:33:44:55 &>/dev/null
	check_fail $? "Changing swp2's MAC address permitted"
	ip link set dev $swp2 addr 00:11:22:33:44:55 2>&1 >/dev/null \
	    | grep -q mlxsw_spectrum
	check_err $? "no extack for bridge port MAC address change rejection"

	log_test "RIF - attach port with bad MAC to bridge"

	ip link del dev d
	ip link del dev br1
	ip addr del dev $swp1 192.0.2.1/28
}

rif_non_inherit_bridge_addr_test()
{
	local swp2_mac=$(mac_get $swp2)

	RET=0

	# Create first RIF
	ip addr add dev $swp1 192.0.2.1/28
	check_err $?

	# Create a FID RIF
	ip link add name br1 up type bridge vlan_filtering 0
	ip link set dev br1 addr $swp2_mac
	ip link set dev $swp2 master br1
	ip addr add dev br1 192.0.2.17/28
	check_err $?

	# Prepare a device with a low MAC address
	ip link add name d up type dummy
	ip link set dev d addr 00:11:22:33:44:55

	# Attach the device to br1. Since the bridge address was set, it should
	# work.
	ip link set dev d master br1 &>/dev/null
	check_err $? "Could not attach a device with low MAC to a bridge with RIF"

	# Port MAC address change should be allowed for a bridge with set MAC.
	ip link set dev $swp2 addr 00:11:22:33:44:55
	check_err $? "Changing swp2's MAC address not permitted"

	log_test "RIF - attach port with bad MAC to bridge with set MAC"

	ip link set dev $swp2 addr $swp2_mac
	ip link del dev d
	ip link del dev br1
	ip addr del dev $swp1 192.0.2.1/28
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS
