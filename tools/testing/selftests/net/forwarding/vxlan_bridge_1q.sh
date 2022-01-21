#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# +-----------------------+                          +------------------------+
# | H1 (vrf)              |                          | H2 (vrf)               |
# |  + $h1.10             |                          |  + $h2.10              |
# |  | 192.0.2.1/28       |                          |  | 192.0.2.2/28        |
# |  |                    |                          |  |                     |
# |  | + $h1.20           |                          |  | + $h2.20            |
# |  \ | 198.51.100.1/24  |                          |  \ | 198.51.100.2/24   |
# |   \|                  |                          |   \|                   |
# |    + $h1              |                          |    + $h2               |
# +----|------------------+                          +----|-------------------+
#      |                                                  |
# +----|--------------------------------------------------|-------------------+
# | SW |                                                  |                   |
# | +--|--------------------------------------------------|-----------------+ |
# | |  + $swp1                   BR1 (802.1q)             + $swp2           | |
# | |     vid 10                                             vid 10         | |
# | |     vid 20                                             vid 20         | |
# | |                                                                       | |
# | |  + vx10 (vxlan)                        + vx20 (vxlan)                 | |
# | |    local 192.0.2.17                      local 192.0.2.17             | |
# | |    remote 192.0.2.34 192.0.2.50          remote 192.0.2.34 192.0.2.50 | |
# | |    id 1000 dstport $VXPORT               id 2000 dstport $VXPORT      | |
# | |    vid 10 pvid untagged                  vid 20 pvid untagged         | |
# | +-----------------------------------------------------------------------+ |
# |                                                                           |
# |  192.0.2.32/28 via 192.0.2.18                                             |
# |  192.0.2.48/28 via 192.0.2.18                                             |
# |                                                                           |
# |    + $rp1                                                                 |
# |    | 192.0.2.17/28                                                        |
# +----|----------------------------------------------------------------------+
#      |
# +----|--------------------------------------------------------+
# |    |                                             VRP2 (vrf) |
# |    + $rp2                                                   |
# |      192.0.2.18/28                                          |
# |                                                             |   (maybe) HW
# =============================================================================
# |                                                             |  (likely) SW
# |    + v1 (veth)                             + v3 (veth)      |
# |    | 192.0.2.33/28                         | 192.0.2.49/28  |
# +----|---------------------------------------|----------------+
#      |                                       |
# +----|------------------------------+   +----|------------------------------+
# |    + v2 (veth)        NS1 (netns) |   |    + v4 (veth)        NS2 (netns) |
# |      192.0.2.34/28                |   |      192.0.2.50/28                |
# |                                   |   |                                   |
# |   192.0.2.16/28 via 192.0.2.33    |   |   192.0.2.16/28 via 192.0.2.49    |
# |   192.0.2.50/32 via 192.0.2.33    |   |   192.0.2.34/32 via 192.0.2.49    |
# |                                   |   |                                   |
# | +-------------------------------+ |   | +-------------------------------+ |
# | |                  BR2 (802.1q) | |   | |                  BR2 (802.1q) | |
# | |  + vx10 (vxlan)               | |   | |  + vx10 (vxlan)               | |
# | |    local 192.0.2.34           | |   | |    local 192.0.2.50           | |
# | |    remote 192.0.2.17          | |   | |    remote 192.0.2.17          | |
# | |    remote 192.0.2.50          | |   | |    remote 192.0.2.34          | |
# | |    id 1000 dstport $VXPORT    | |   | |    id 1000 dstport $VXPORT    | |
# | |    vid 10 pvid untagged       | |   | |    vid 10 pvid untagged       | |
# | |                               | |   | |                               | |
# | |  + vx20 (vxlan)               | |   | |  + vx20 (vxlan)               | |
# | |    local 192.0.2.34           | |   | |    local 192.0.2.50           | |
# | |    remote 192.0.2.17          | |   | |    remote 192.0.2.17          | |
# | |    remote 192.0.2.50          | |   | |    remote 192.0.2.34          | |
# | |    id 2000 dstport $VXPORT    | |   | |    id 2000 dstport $VXPORT    | |
# | |    vid 20 pvid untagged       | |   | |    vid 20 pvid untagged       | |
# | |                               | |   | |                               | |
# | |  + w1 (veth)                  | |   | |  + w1 (veth)                  | |
# | |  | vid 10                     | |   | |  | vid 10                     | |
# | |  | vid 20                     | |   | |  | vid 20                     | |
# | +--|----------------------------+ |   | +--|----------------------------+ |
# |    |                              |   |    |                              |
# | +--|----------------------------+ |   | +--|----------------------------+ |
# | |  + w2 (veth)        VW2 (vrf) | |   | |  + w2 (veth)        VW2 (vrf) | |
# | |  |\                           | |   | |  |\                           | |
# | |  | + w2.10                    | |   | |  | + w2.10                    | |
# | |  |   192.0.2.3/28             | |   | |  |   192.0.2.4/28             | |
# | |  |                            | |   | |  |                            | |
# | |  + w2.20                      | |   | |  + w2.20                      | |
# | |    198.51.100.3/24            | |   | |    198.51.100.4/24            | |
# | +-------------------------------+ |   | +-------------------------------+ |
# +-----------------------------------+   +-----------------------------------+

: ${VXPORT:=4789}
export VXPORT

: ${ALL_TESTS:="
	ping_ipv4
	test_flood
	test_unicast
	reapply_config
	ping_ipv4
	test_flood
	test_unicast
	test_learning
	test_pvid
    "}

NUM_NETIFS=6
source lib.sh

h1_create()
{
	simple_if_init $h1
	tc qdisc add dev $h1 clsact
	vlan_create $h1 10 v$h1 192.0.2.1/28
	vlan_create $h1 20 v$h1 198.51.100.1/24
}

h1_destroy()
{
	vlan_destroy $h1 20
	vlan_destroy $h1 10
	tc qdisc del dev $h1 clsact
	simple_if_fini $h1
}

h2_create()
{
	simple_if_init $h2
	tc qdisc add dev $h2 clsact
	vlan_create $h2 10 v$h2 192.0.2.2/28
	vlan_create $h2 20 v$h2 198.51.100.2/24
}

h2_destroy()
{
	vlan_destroy $h2 20
	vlan_destroy $h2 10
	tc qdisc del dev $h2 clsact
	simple_if_fini $h2
}

rp1_set_addr()
{
	ip address add dev $rp1 192.0.2.17/28

	ip route add 192.0.2.32/28 nexthop via 192.0.2.18
	ip route add 192.0.2.48/28 nexthop via 192.0.2.18
}

rp1_unset_addr()
{
	ip route del 192.0.2.48/28 nexthop via 192.0.2.18
	ip route del 192.0.2.32/28 nexthop via 192.0.2.18

	ip address del dev $rp1 192.0.2.17/28
}

switch_create()
{
	ip link add name br1 type bridge vlan_filtering 1 vlan_default_pvid 0 \
		mcast_snooping 0
	# Make sure the bridge uses the MAC address of the local port and not
	# that of the VxLAN's device.
	ip link set dev br1 address $(mac_get $swp1)
	ip link set dev br1 up

	ip link set dev $rp1 up
	rp1_set_addr

	ip link add name vx10 type vxlan id 1000	\
		local 192.0.2.17 dstport "$VXPORT"	\
		nolearning noudpcsum tos inherit ttl 100
	ip link set dev vx10 up

	ip link set dev vx10 master br1
	bridge vlan add vid 10 dev vx10 pvid untagged

	ip link add name vx20 type vxlan id 2000	\
		local 192.0.2.17 dstport "$VXPORT"	\
		nolearning noudpcsum tos inherit ttl 100
	ip link set dev vx20 up

	ip link set dev vx20 master br1
	bridge vlan add vid 20 dev vx20 pvid untagged

	ip link set dev $swp1 master br1
	ip link set dev $swp1 up
	bridge vlan add vid 10 dev $swp1
	bridge vlan add vid 20 dev $swp1

	ip link set dev $swp2 master br1
	ip link set dev $swp2 up
	bridge vlan add vid 10 dev $swp2
	bridge vlan add vid 20 dev $swp2

	bridge fdb append dev vx10 00:00:00:00:00:00 dst 192.0.2.34 self
	bridge fdb append dev vx10 00:00:00:00:00:00 dst 192.0.2.50 self

	bridge fdb append dev vx20 00:00:00:00:00:00 dst 192.0.2.34 self
	bridge fdb append dev vx20 00:00:00:00:00:00 dst 192.0.2.50 self
}

switch_destroy()
{
	bridge fdb del dev vx20 00:00:00:00:00:00 dst 192.0.2.50 self
	bridge fdb del dev vx20 00:00:00:00:00:00 dst 192.0.2.34 self

	bridge fdb del dev vx10 00:00:00:00:00:00 dst 192.0.2.50 self
	bridge fdb del dev vx10 00:00:00:00:00:00 dst 192.0.2.34 self

	bridge vlan del vid 20 dev $swp2
	bridge vlan del vid 10 dev $swp2
	ip link set dev $swp2 down
	ip link set dev $swp2 nomaster

	bridge vlan del vid 20 dev $swp1
	bridge vlan del vid 10 dev $swp1
	ip link set dev $swp1 down
	ip link set dev $swp1 nomaster

	bridge vlan del vid 20 dev vx20
	ip link set dev vx20 nomaster

	ip link set dev vx20 down
	ip link del dev vx20

	bridge vlan del vid 10 dev vx10
	ip link set dev vx10 nomaster

	ip link set dev vx10 down
	ip link del dev vx10

	rp1_unset_addr
	ip link set dev $rp1 down

	ip link set dev br1 down
	ip link del dev br1
}

vrp2_create()
{
	simple_if_init $rp2 192.0.2.18/28
	__simple_if_init v1 v$rp2 192.0.2.33/28
	__simple_if_init v3 v$rp2 192.0.2.49/28
	tc qdisc add dev v1 clsact
}

vrp2_destroy()
{
	tc qdisc del dev v1 clsact
	__simple_if_fini v3 192.0.2.49/28
	__simple_if_fini v1 192.0.2.33/28
	simple_if_fini $rp2 192.0.2.18/28
}

ns_init_common()
{
	local in_if=$1; shift
	local in_addr=$1; shift
	local other_in_addr=$1; shift
	local nh_addr=$1; shift
	local host_addr1=$1; shift
	local host_addr2=$1; shift

	ip link set dev $in_if up
	ip address add dev $in_if $in_addr/28
	tc qdisc add dev $in_if clsact

	ip link add name br2 type bridge vlan_filtering 1 vlan_default_pvid 0
	ip link set dev br2 up

	ip link add name w1 type veth peer name w2

	ip link set dev w1 master br2
	ip link set dev w1 up

	bridge vlan add vid 10 dev w1
	bridge vlan add vid 20 dev w1

	ip link add name vx10 type vxlan id 1000 local $in_addr \
		dstport "$VXPORT"
	ip link set dev vx10 up
	bridge fdb append dev vx10 00:00:00:00:00:00 dst 192.0.2.17 self
	bridge fdb append dev vx10 00:00:00:00:00:00 dst $other_in_addr self

	ip link set dev vx10 master br2
	tc qdisc add dev vx10 clsact

	bridge vlan add vid 10 dev vx10 pvid untagged

	ip link add name vx20 type vxlan id 2000 local $in_addr \
		dstport "$VXPORT"
	ip link set dev vx20 up
	bridge fdb append dev vx20 00:00:00:00:00:00 dst 192.0.2.17 self
	bridge fdb append dev vx20 00:00:00:00:00:00 dst $other_in_addr self

	ip link set dev vx20 master br2
	tc qdisc add dev vx20 clsact

	bridge vlan add vid 20 dev vx20 pvid untagged

	simple_if_init w2
        vlan_create w2 10 vw2 $host_addr1/28
        vlan_create w2 20 vw2 $host_addr2/24

	ip route add 192.0.2.16/28 nexthop via $nh_addr
	ip route add $other_in_addr/32 nexthop via $nh_addr
}
export -f ns_init_common

ns1_create()
{
	ip netns add ns1
	ip link set dev v2 netns ns1
	in_ns ns1 \
	      ns_init_common v2 192.0.2.34 192.0.2.50 192.0.2.33 192.0.2.3 \
	      198.51.100.3
}

ns1_destroy()
{
	ip netns exec ns1 ip link set dev v2 netns 1
	ip netns del ns1
}

ns2_create()
{
	ip netns add ns2
	ip link set dev v4 netns ns2
	in_ns ns2 \
	      ns_init_common v4 192.0.2.50 192.0.2.34 192.0.2.49 192.0.2.4 \
	      198.51.100.4
}

ns2_destroy()
{
	ip netns exec ns2 ip link set dev v4 netns 1
	ip netns del ns2
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	swp1=${NETIFS[p2]}

	swp2=${NETIFS[p3]}
	h2=${NETIFS[p4]}

	rp1=${NETIFS[p5]}
	rp2=${NETIFS[p6]}

	vrf_prepare
	forwarding_enable

	h1_create
	h2_create
	switch_create

	ip link add name v1 type veth peer name v2
	ip link add name v3 type veth peer name v4
	vrp2_create
	ns1_create
	ns2_create

	r1_mac=$(in_ns ns1 mac_get w2)
	r2_mac=$(in_ns ns2 mac_get w2)
	h2_mac=$(mac_get $h2)
}

cleanup()
{
	pre_cleanup

	ns2_destroy
	ns1_destroy
	vrp2_destroy
	ip link del dev v3
	ip link del dev v1

	switch_destroy
	h2_destroy
	h1_destroy

	forwarding_restore
	vrf_cleanup
}

# For the first round of tests, vx10 and vx20 were the first devices to get
# attached to the bridge, and that at the point that the local IP is already
# configured. Try the other scenario of attaching these devices to a bridge
# that already has local ports members, and only then assign the local IP.
reapply_config()
{
	log_info "Reapplying configuration"

	bridge fdb del dev vx20 00:00:00:00:00:00 dst 192.0.2.50 self
	bridge fdb del dev vx20 00:00:00:00:00:00 dst 192.0.2.34 self

	bridge fdb del dev vx10 00:00:00:00:00:00 dst 192.0.2.50 self
	bridge fdb del dev vx10 00:00:00:00:00:00 dst 192.0.2.34 self

	ip link set dev vx20 nomaster
	ip link set dev vx10 nomaster

	rp1_unset_addr
	sleep 5

	ip link set dev vx10 master br1
	bridge vlan add vid 10 dev vx10 pvid untagged

	ip link set dev vx20 master br1
	bridge vlan add vid 20 dev vx20 pvid untagged

	bridge fdb append dev vx10 00:00:00:00:00:00 dst 192.0.2.34 self
	bridge fdb append dev vx10 00:00:00:00:00:00 dst 192.0.2.50 self

	bridge fdb append dev vx20 00:00:00:00:00:00 dst 192.0.2.34 self
	bridge fdb append dev vx20 00:00:00:00:00:00 dst 192.0.2.50 self

	rp1_set_addr
	sleep 5
}

ping_ipv4()
{
	ping_test $h1.10 192.0.2.2 ": local->local vid 10"
	ping_test $h1.20 198.51.100.2 ": local->local vid 20"
	ping_test $h1.10 192.0.2.3 ": local->remote 1 vid 10"
	ping_test $h1.10 192.0.2.4 ": local->remote 2 vid 10"
	ping_test $h1.20 198.51.100.3 ": local->remote 1 vid 20"
	ping_test $h1.20 198.51.100.4 ": local->remote 2 vid 20"
}

maybe_in_ns()
{
	echo ${1:+in_ns} $1
}

__flood_counter_add_del()
{
	local add_del=$1; shift
	local dev=$1; shift
	local ns=$1; shift

	# Putting the ICMP capture both to HW and to SW will end up
	# double-counting the packets that are trapped to slow path, such as for
	# the unicast test. Adding either skip_hw or skip_sw fixes this problem,
	# but with skip_hw, the flooded packets are not counted at all, because
	# those are dropped due to MAC address mismatch; and skip_sw is a no-go
	# for veth-based topologies.
	#
	# So try to install with skip_sw and fall back to skip_sw if that fails.

	$(maybe_in_ns $ns) __icmp_capture_add_del          \
			   $add_del 100 "" $dev skip_sw 2>/dev/null || \
	$(maybe_in_ns $ns) __icmp_capture_add_del          \
			   $add_del 100 "" $dev skip_hw
}

flood_counter_install()
{
	__flood_counter_add_del add "$@"
}

flood_counter_uninstall()
{
	__flood_counter_add_del del "$@"
}

flood_fetch_stat()
{
	local dev=$1; shift
	local ns=$1; shift

	$(maybe_in_ns $ns) tc_rule_stats_get $dev 100 ingress
}

flood_fetch_stats()
{
	local counters=("${@}")
	local counter

	for counter in "${counters[@]}"; do
		flood_fetch_stat $counter
	done
}

vxlan_flood_test()
{
	local mac=$1; shift
	local dst=$1; shift
	local vid=$1; shift
	local -a expects=("${@}")

	local -a counters=($h2 "vx10 ns1" "vx20 ns1" "vx10 ns2" "vx20 ns2")
	local counter
	local key

	# Packets reach the local host tagged whereas they reach the VxLAN
	# devices untagged. In order to be able to use the same filter for
	# all counters, make sure the packets also reach the local host
	# untagged
	bridge vlan add vid $vid dev $swp2 untagged
	for counter in "${counters[@]}"; do
		flood_counter_install $counter
	done

	local -a t0s=($(flood_fetch_stats "${counters[@]}"))
	$MZ $h1 -Q $vid -c 10 -d 100msec -p 64 -b $mac -B $dst -t icmp -q
	sleep 1
	local -a t1s=($(flood_fetch_stats "${counters[@]}"))

	for key in ${!t0s[@]}; do
		local delta=$((t1s[$key] - t0s[$key]))
		local expect=${expects[$key]}

		((expect == delta))
		check_err $? "${counters[$key]}: Expected to capture $expect packets, got $delta."
	done

	for counter in "${counters[@]}"; do
		flood_counter_uninstall $counter
	done
	bridge vlan add vid $vid dev $swp2
}

__test_flood()
{
	local mac=$1; shift
	local dst=$1; shift
	local vid=$1; shift
	local what=$1; shift
	local -a expects=("${@}")

	RET=0

	vxlan_flood_test $mac $dst $vid "${expects[@]}"

	log_test "VXLAN: $what"
}

test_flood()
{
	__test_flood de:ad:be:ef:13:37 192.0.2.100 10 "flood vlan 10" \
		10 10 0 10 0
	__test_flood ca:fe:be:ef:13:37 198.51.100.100 20 "flood vlan 20" \
		10 0 10 0 10
}

vxlan_fdb_add_del()
{
	local add_del=$1; shift
	local vid=$1; shift
	local mac=$1; shift
	local dev=$1; shift
	local dst=$1; shift

	bridge fdb $add_del dev $dev $mac self static permanent \
		${dst:+dst} $dst 2>/dev/null
	bridge fdb $add_del dev $dev $mac master static vlan $vid 2>/dev/null
}

__test_unicast()
{
	local mac=$1; shift
	local dst=$1; shift
	local hit_idx=$1; shift
	local vid=$1; shift
	local what=$1; shift

	RET=0

	local -a expects=(0 0 0 0 0)
	expects[$hit_idx]=10

	vxlan_flood_test $mac $dst $vid "${expects[@]}"

	log_test "VXLAN: $what"
}

test_unicast()
{
	local -a targets=("$h2_mac $h2"
			  "$r1_mac vx10 192.0.2.34"
			  "$r2_mac vx10 192.0.2.50")
	local target

	log_info "unicast vlan 10"

	for target in "${targets[@]}"; do
		vxlan_fdb_add_del add 10 $target
	done

	__test_unicast $h2_mac 192.0.2.2 0 10 "local MAC unicast"
	__test_unicast $r1_mac 192.0.2.3 1 10 "remote MAC 1 unicast"
	__test_unicast $r2_mac 192.0.2.4 3 10 "remote MAC 2 unicast"

	for target in "${targets[@]}"; do
		vxlan_fdb_add_del del 10 $target
	done

	log_info "unicast vlan 20"

	targets=("$h2_mac $h2" "$r1_mac vx20 192.0.2.34" \
		 "$r2_mac vx20 192.0.2.50")

	for target in "${targets[@]}"; do
		vxlan_fdb_add_del add 20 $target
	done

	__test_unicast $h2_mac 198.51.100.2 0 20 "local MAC unicast"
	__test_unicast $r1_mac 198.51.100.3 2 20 "remote MAC 1 unicast"
	__test_unicast $r2_mac 198.51.100.4 4 20 "remote MAC 2 unicast"

	for target in "${targets[@]}"; do
		vxlan_fdb_add_del del 20 $target
	done
}

test_pvid()
{
	local -a expects=(0 0 0 0 0)
	local mac=de:ad:be:ef:13:37
	local dst=192.0.2.100
	local vid=10

	# Check that flooding works
	RET=0

	expects[0]=10; expects[1]=10; expects[3]=10
	vxlan_flood_test $mac $dst $vid "${expects[@]}"

	log_test "VXLAN: flood before pvid off"

	# Toggle PVID off and test that flood to remote hosts does not work
	RET=0

	bridge vlan add vid 10 dev vx10

	expects[0]=10; expects[1]=0; expects[3]=0
	vxlan_flood_test $mac $dst $vid "${expects[@]}"

	log_test "VXLAN: flood after pvid off"

	# Toggle PVID on and test that flood to remote hosts does work
	RET=0

	bridge vlan add vid 10 dev vx10 pvid untagged

	expects[0]=10; expects[1]=10; expects[3]=10
	vxlan_flood_test $mac $dst $vid "${expects[@]}"

	log_test "VXLAN: flood after pvid on"

	# Add a new VLAN and test that it does not affect flooding
	RET=0

	bridge vlan add vid 30 dev vx10

	expects[0]=10; expects[1]=10; expects[3]=10
	vxlan_flood_test $mac $dst $vid "${expects[@]}"

	bridge vlan del vid 30 dev vx10

	log_test "VXLAN: flood after vlan add"

	# Remove currently mapped VLAN and test that flood to remote hosts does
	# not work
	RET=0

	bridge vlan del vid 10 dev vx10

	expects[0]=10; expects[1]=0; expects[3]=0
	vxlan_flood_test $mac $dst $vid "${expects[@]}"

	log_test "VXLAN: flood after vlan delete"

	# Re-add the VLAN and test that flood to remote hosts does work
	RET=0

	bridge vlan add vid 10 dev vx10 pvid untagged

	expects[0]=10; expects[1]=10; expects[3]=10
	vxlan_flood_test $mac $dst $vid "${expects[@]}"

	log_test "VXLAN: flood after vlan re-add"
}

__test_learning()
{
	local -a expects=(0 0 0 0 0)
	local mac=$1; shift
	local dst=$1; shift
	local vid=$1; shift
	local idx1=$1; shift
	local idx2=$1; shift
	local vx=vx$vid

	# Check that flooding works
	RET=0

	expects[0]=10; expects[$idx1]=10; expects[$idx2]=10
	vxlan_flood_test $mac $dst $vid "${expects[@]}"

	log_test "VXLAN: flood before learning"

	# Send a packet with source mac set to $mac from host w2 and check that
	# a corresponding entry is created in the VxLAN device
	RET=0

	in_ns ns1 $MZ w2 -Q $vid -c 1 -p 64 -a $mac -b ff:ff:ff:ff:ff:ff \
		-B $dst -t icmp -q
	sleep 1

	bridge fdb show brport $vx | grep $mac | grep -q self
	check_err $?
	bridge fdb show brport $vx | grep $mac | grep "vlan $vid" \
		| grep -q -v self
	check_err $?

	log_test "VXLAN: show learned FDB entry"

	# Repeat first test and check that packets only reach host w2 in ns1
	RET=0

	expects[0]=0; expects[$idx1]=10; expects[$idx2]=0
	vxlan_flood_test $mac $dst $vid "${expects[@]}"

	log_test "VXLAN: learned FDB entry"

	# Delete the learned FDB entry from the VxLAN and bridge devices and
	# check that packets are flooded
	RET=0

	bridge fdb del dev $vx $mac master self vlan $vid
	sleep 1

	expects[0]=10; expects[$idx1]=10; expects[$idx2]=10
	vxlan_flood_test $mac $dst $vid "${expects[@]}"

	log_test "VXLAN: deletion of learned FDB entry"

	# Re-learn the first FDB entry and check that it is correctly aged-out
	RET=0

	in_ns ns1 $MZ w2 -Q $vid -c 1 -p 64 -a $mac -b ff:ff:ff:ff:ff:ff \
		-B $dst -t icmp -q
	sleep 1

	bridge fdb show brport $vx | grep $mac | grep -q self
	check_err $?
	bridge fdb show brport $vx | grep $mac | grep "vlan $vid" \
		| grep -q -v self
	check_err $?

	expects[0]=0; expects[$idx1]=10; expects[$idx2]=0
	vxlan_flood_test $mac $dst $vid "${expects[@]}"

	sleep 20

	bridge fdb show brport $vx | grep $mac | grep -q self
	check_fail $?
	bridge fdb show brport $vx | grep $mac | grep "vlan $vid" \
		| grep -q -v self
	check_fail $?

	expects[0]=10; expects[$idx1]=10; expects[$idx2]=10
	vxlan_flood_test $mac $dst $vid "${expects[@]}"

	log_test "VXLAN: Ageing of learned FDB entry"

	# Toggle learning on the bridge port and check that the bridge's FDB
	# is populated only when it should
	RET=0

	ip link set dev $vx type bridge_slave learning off

	in_ns ns1 $MZ w2 -Q $vid -c 1 -p 64 -a $mac -b ff:ff:ff:ff:ff:ff \
		-B $dst -t icmp -q
	sleep 1

	bridge fdb show brport $vx | grep $mac | grep "vlan $vid" \
		| grep -q -v self
	check_fail $?

	ip link set dev $vx type bridge_slave learning on

	in_ns ns1 $MZ w2 -Q $vid -c 1 -p 64 -a $mac -b ff:ff:ff:ff:ff:ff \
		-B $dst -t icmp -q
	sleep 1

	bridge fdb show brport $vx | grep $mac | grep "vlan $vid" \
		| grep -q -v self
	check_err $?

	log_test "VXLAN: learning toggling on bridge port"
}

test_learning()
{
	local mac=de:ad:be:ef:13:37
	local dst=192.0.2.100
	local vid=10

	# Enable learning on the VxLAN devices and set ageing time to 10 seconds
	ip link set dev br1 type bridge ageing_time 1000
	ip link set dev vx10 type vxlan ageing 10
	ip link set dev vx10 type vxlan learning
	ip link set dev vx20 type vxlan ageing 10
	ip link set dev vx20 type vxlan learning
	reapply_config

	log_info "learning vlan 10"

	__test_learning $mac $dst $vid 1 3

	log_info "learning vlan 20"

	mac=ca:fe:be:ef:13:37
	dst=198.51.100.100
	vid=20

	__test_learning $mac $dst $vid 2 4

	# Restore previous settings
	ip link set dev vx20 type vxlan nolearning
	ip link set dev vx20 type vxlan ageing 300
	ip link set dev vx10 type vxlan nolearning
	ip link set dev vx10 type vxlan ageing 300
	ip link set dev br1 type bridge ageing_time 30000
	reapply_config
}

test_all()
{
	log_info "Running tests with UDP port $VXPORT"
	tests_run
}

trap cleanup EXIT

setup_prepare
setup_wait
test_all

exit $EXIT_STATUS
