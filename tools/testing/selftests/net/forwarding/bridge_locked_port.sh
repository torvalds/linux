#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

ALL_TESTS="
	locked_port_ipv4
	locked_port_ipv6
	locked_port_vlan
	locked_port_mab
	locked_port_mab_roam
	locked_port_mab_config
	locked_port_mab_flush
	locked_port_mab_redirect
"

NUM_NETIFS=4
CHECK_TC="no"
source lib.sh

h1_create()
{
	simple_if_init $h1 192.0.2.1/24 2001:db8:1::1/64
	vlan_create $h1 100 v$h1 198.51.100.1/24
}

h1_destroy()
{
	vlan_destroy $h1 100
	simple_if_fini $h1 192.0.2.1/24 2001:db8:1::1/64
}

h2_create()
{
	simple_if_init $h2 192.0.2.2/24 2001:db8:1::2/64
	vlan_create $h2 100 v$h2 198.51.100.2/24
}

h2_destroy()
{
	vlan_destroy $h2 100
	simple_if_fini $h2 192.0.2.2/24 2001:db8:1::2/64
}

switch_create()
{
	ip link add dev br0 type bridge vlan_filtering 1

	ip link set dev $swp1 master br0
	ip link set dev $swp2 master br0

	bridge link set dev $swp1 learning off

	ip link set dev br0 up
	ip link set dev $swp1 up
	ip link set dev $swp2 up
}

switch_destroy()
{
	ip link set dev $swp2 down
	ip link set dev $swp1 down

	ip link del dev br0
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	swp1=${NETIFS[p2]}

	swp2=${NETIFS[p3]}
	h2=${NETIFS[p4]}

	vrf_prepare

	h1_create
	h2_create

	switch_create
}

cleanup()
{
	pre_cleanup

	switch_destroy

	h2_destroy
	h1_destroy

	vrf_cleanup
}

locked_port_ipv4()
{
	RET=0

	check_locked_port_support || return 0

	ping_do $h1 192.0.2.2
	check_err $? "Ping did not work before locking port"

	bridge link set dev $swp1 locked on

	ping_do $h1 192.0.2.2
	check_fail $? "Ping worked after locking port, but before adding FDB entry"

	bridge fdb add `mac_get $h1` dev $swp1 master static

	ping_do $h1 192.0.2.2
	check_err $? "Ping did not work after locking port and adding FDB entry"

	bridge link set dev $swp1 locked off
	bridge fdb del `mac_get $h1` dev $swp1 master static

	ping_do $h1 192.0.2.2
	check_err $? "Ping did not work after unlocking port and removing FDB entry."

	log_test "Locked port ipv4"
}

locked_port_vlan()
{
	RET=0

	check_locked_port_support || return 0

	bridge vlan add vid 100 dev $swp1
	bridge vlan add vid 100 dev $swp2

	ping_do $h1.100 198.51.100.2
	check_err $? "Ping through vlan did not work before locking port"

	bridge link set dev $swp1 locked on
	ping_do $h1.100 198.51.100.2
	check_fail $? "Ping through vlan worked after locking port, but before adding FDB entry"

	bridge fdb add `mac_get $h1` dev $swp1 vlan 100 master static

	ping_do $h1.100 198.51.100.2
	check_err $? "Ping through vlan did not work after locking port and adding FDB entry"

	bridge link set dev $swp1 locked off
	bridge fdb del `mac_get $h1` dev $swp1 vlan 100 master static

	ping_do $h1.100 198.51.100.2
	check_err $? "Ping through vlan did not work after unlocking port and removing FDB entry"

	bridge vlan del vid 100 dev $swp1
	bridge vlan del vid 100 dev $swp2
	log_test "Locked port vlan"
}

locked_port_ipv6()
{
	RET=0
	check_locked_port_support || return 0

	ping6_do $h1 2001:db8:1::2
	check_err $? "Ping6 did not work before locking port"

	bridge link set dev $swp1 locked on

	ping6_do $h1 2001:db8:1::2
	check_fail $? "Ping6 worked after locking port, but before adding FDB entry"

	bridge fdb add `mac_get $h1` dev $swp1 master static
	ping6_do $h1 2001:db8:1::2
	check_err $? "Ping6 did not work after locking port and adding FDB entry"

	bridge link set dev $swp1 locked off
	bridge fdb del `mac_get $h1` dev $swp1 master static

	ping6_do $h1 2001:db8:1::2
	check_err $? "Ping6 did not work after unlocking port and removing FDB entry"

	log_test "Locked port ipv6"
}

locked_port_mab()
{
	RET=0
	check_port_mab_support || return 0

	ping_do $h1 192.0.2.2
	check_err $? "Ping did not work before locking port"

	bridge link set dev $swp1 learning on locked on

	ping_do $h1 192.0.2.2
	check_fail $? "Ping worked on a locked port without an FDB entry"

	bridge fdb get `mac_get $h1` br br0 vlan 1 &> /dev/null
	check_fail $? "FDB entry created before enabling MAB"

	bridge link set dev $swp1 learning on locked on mab on

	ping_do $h1 192.0.2.2
	check_fail $? "Ping worked on MAB enabled port without an FDB entry"

	bridge fdb get `mac_get $h1` br br0 vlan 1 | grep "dev $swp1" | grep -q "locked"
	check_err $? "Locked FDB entry not created"

	bridge fdb replace `mac_get $h1` dev $swp1 master static

	ping_do $h1 192.0.2.2
	check_err $? "Ping did not work after replacing FDB entry"

	bridge fdb get `mac_get $h1` br br0 vlan 1 | grep "dev $swp1" | grep -q "locked"
	check_fail $? "FDB entry marked as locked after replacement"

	bridge fdb del `mac_get $h1` dev $swp1 master
	bridge link set dev $swp1 learning off locked off mab off

	log_test "Locked port MAB"
}

# Check that entries cannot roam to a locked port, but that entries can roam
# to an unlocked port.
locked_port_mab_roam()
{
	local mac=a0:b0:c0:c0:b0:a0

	RET=0
	check_port_mab_support || return 0

	bridge link set dev $swp1 learning on locked on mab on

	$MZ $h1 -q -c 5 -d 100msec -t udp -a $mac -b rand
	bridge fdb get $mac br br0 vlan 1 | grep "dev $swp1" | grep -q "locked"
	check_err $? "No locked entry on first injection"

	$MZ $h2 -q -c 5 -d 100msec -t udp -a $mac -b rand
	bridge fdb get $mac br br0 vlan 1 | grep -q "dev $swp2"
	check_err $? "Entry did not roam to an unlocked port"

	bridge fdb get $mac br br0 vlan 1 | grep -q "locked"
	check_fail $? "Entry roamed with locked flag on"

	$MZ $h1 -q -c 5 -d 100msec -t udp -a $mac -b rand
	bridge fdb get $mac br br0 vlan 1 | grep -q "dev $swp1"
	check_fail $? "Entry roamed back to locked port"

	bridge fdb del $mac vlan 1 dev $swp2 master
	bridge link set dev $swp1 learning off locked off mab off

	log_test "Locked port MAB roam"
}

# Check that MAB can only be enabled on a port that is both locked and has
# learning enabled.
locked_port_mab_config()
{
	RET=0
	check_port_mab_support || return 0

	bridge link set dev $swp1 learning on locked off mab on &> /dev/null
	check_fail $? "MAB enabled while port is unlocked"

	bridge link set dev $swp1 learning off locked on mab on &> /dev/null
	check_fail $? "MAB enabled while port has learning disabled"

	bridge link set dev $swp1 learning on locked on mab on
	check_err $? "Failed to enable MAB when port is locked and has learning enabled"

	bridge link set dev $swp1 learning off locked off mab off

	log_test "Locked port MAB configuration"
}

# Check that locked FDB entries are flushed from a port when MAB is disabled.
locked_port_mab_flush()
{
	local locked_mac1=00:01:02:03:04:05
	local unlocked_mac1=00:01:02:03:04:06
	local locked_mac2=00:01:02:03:04:07
	local unlocked_mac2=00:01:02:03:04:08

	RET=0
	check_port_mab_support || return 0

	bridge link set dev $swp1 learning on locked on mab on
	bridge link set dev $swp2 learning on locked on mab on

	# Create regular and locked FDB entries on each port.
	bridge fdb add $unlocked_mac1 dev $swp1 vlan 1 master static
	bridge fdb add $unlocked_mac2 dev $swp2 vlan 1 master static

	$MZ $h1 -q -c 5 -d 100msec -t udp -a $locked_mac1 -b rand
	bridge fdb get $locked_mac1 br br0 vlan 1 | grep "dev $swp1" | \
		grep -q "locked"
	check_err $? "Failed to create locked FDB entry on first port"

	$MZ $h2 -q -c 5 -d 100msec -t udp -a $locked_mac2 -b rand
	bridge fdb get $locked_mac2 br br0 vlan 1 | grep "dev $swp2" | \
		grep -q "locked"
	check_err $? "Failed to create locked FDB entry on second port"

	# Disable MAB on the first port and check that only the first locked
	# FDB entry was flushed.
	bridge link set dev $swp1 mab off

	bridge fdb get $unlocked_mac1 br br0 vlan 1 &> /dev/null
	check_err $? "Regular FDB entry on first port was flushed after disabling MAB"

	bridge fdb get $unlocked_mac2 br br0 vlan 1 &> /dev/null
	check_err $? "Regular FDB entry on second port was flushed after disabling MAB"

	bridge fdb get $locked_mac1 br br0 vlan 1 &> /dev/null
	check_fail $? "Locked FDB entry on first port was not flushed after disabling MAB"

	bridge fdb get $locked_mac2 br br0 vlan 1 &> /dev/null
	check_err $? "Locked FDB entry on second port was flushed after disabling MAB"

	bridge fdb del $unlocked_mac2 dev $swp2 vlan 1 master static
	bridge fdb del $unlocked_mac1 dev $swp1 vlan 1 master static

	bridge link set dev $swp2 learning on locked off mab off
	bridge link set dev $swp1 learning off locked off mab off

	log_test "Locked port MAB FDB flush"
}

# Check that traffic can be redirected from a locked bridge port and that it
# does not create locked FDB entries.
locked_port_mab_redirect()
{
	RET=0
	check_port_mab_support || return 0

	tc qdisc add dev $swp1 clsact
	tc filter add dev $swp1 ingress protocol all pref 1 handle 101 flower \
		action mirred egress redirect dev $swp2
	bridge link set dev $swp1 learning on locked on mab on

	ping_do $h1 192.0.2.2
	check_err $? "Ping did not work with redirection"

	bridge fdb get `mac_get $h1` br br0 vlan 1 2> /dev/null | \
		grep "dev $swp1" | grep -q "locked"
	check_fail $? "Locked entry created for redirected traffic"

	tc filter del dev $swp1 ingress protocol all pref 1 handle 101 flower

	ping_do $h1 192.0.2.2
	check_fail $? "Ping worked without redirection"

	bridge fdb get `mac_get $h1` br br0 vlan 1 2> /dev/null | \
		grep "dev $swp1" | grep -q "locked"
	check_err $? "Locked entry not created after deleting filter"

	bridge fdb del `mac_get $h1` vlan 1 dev $swp1 master
	bridge link set dev $swp1 learning off locked off mab off
	tc qdisc del dev $swp1 clsact

	log_test "Locked port MAB redirect"
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS
