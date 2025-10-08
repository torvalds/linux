#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# +------------------+
# | H1 (v$h1)        |
# | 2001:db8:1::2/64 |
# | 198.51.100.2/28  |
# |         $h1 +    |
# +-------------|----+
#               |
# +-------------|-------------------------------+
# | SW1         |                               |
# |        $rp1 +                               |
# | 198.51.100.1/28                             |
# | 2001:db8:1::1/64                            |
# |                                             |
# | 2001:db8:2::1/64           2001:db8:3::1/64 |
# | 198.51.100.17/28           198.51.100.33/28 |
# |         $rp2 +                     $rp3 +   |
# +--------------|--------------------------|---+
#                |                          |
#                |                          |
# +--------------|---+       +--------------|---+
# | H2 (v$h2)    |   |       | H3 (v$h3)    |   |
# |          $h2 +   |       |          $h3 +   |
# | 198.51.100.18/28 |       | 198.51.100.34/28 |
# | 2001:db8:2::2/64 |       | 2001:db8:3::2/64 |
# +------------------+       +------------------+
#

ALL_TESTS="mcast_v4 mcast_v6 rpf_v4 rpf_v6 unres_v4 unres_v6"
NUM_NETIFS=6
source lib.sh
source tc_common.sh

h1_create()
{
	simple_if_init $h1 198.51.100.2/28 2001:db8:1::2/64

	ip route add 198.51.100.16/28 vrf v$h1 nexthop via 198.51.100.1
	ip route add 198.51.100.32/28 vrf v$h1 nexthop via 198.51.100.1

	ip route add 2001:db8:2::/64 vrf v$h1 nexthop via 2001:db8:1::1
	ip route add 2001:db8:3::/64 vrf v$h1 nexthop via 2001:db8:1::1

	tc qdisc add dev $h1 ingress
}

h1_destroy()
{
	tc qdisc del dev $h1 ingress

	ip route del 2001:db8:3::/64 vrf v$h1
	ip route del 2001:db8:2::/64 vrf v$h1

	ip route del 198.51.100.32/28 vrf v$h1
	ip route del 198.51.100.16/28 vrf v$h1

	simple_if_fini $h1 198.51.100.2/28 2001:db8:1::2/64
}

h2_create()
{
	simple_if_init $h2 198.51.100.18/28 2001:db8:2::2/64

	ip route add 198.51.100.0/28 vrf v$h2 nexthop via 198.51.100.17
	ip route add 198.51.100.32/28 vrf v$h2 nexthop via 198.51.100.17

	ip route add 2001:db8:1::/64 vrf v$h2 nexthop via 2001:db8:2::1
	ip route add 2001:db8:3::/64 vrf v$h2 nexthop via 2001:db8:2::1

	tc qdisc add dev $h2 ingress
}

h2_destroy()
{
	tc qdisc del dev $h2 ingress

	ip route del 2001:db8:3::/64 vrf v$h2
	ip route del 2001:db8:1::/64 vrf v$h2

	ip route del 198.51.100.32/28 vrf v$h2
	ip route del 198.51.100.0/28 vrf v$h2

	simple_if_fini $h2 198.51.100.18/28 2001:db8:2::2/64
}

h3_create()
{
	simple_if_init $h3 198.51.100.34/28 2001:db8:3::2/64

	ip route add 198.51.100.0/28 vrf v$h3 nexthop via 198.51.100.33
	ip route add 198.51.100.16/28 vrf v$h3 nexthop via 198.51.100.33

	ip route add 2001:db8:1::/64 vrf v$h3 nexthop via 2001:db8:3::1
	ip route add 2001:db8:2::/64 vrf v$h3 nexthop via 2001:db8:3::1

	tc qdisc add dev $h3 ingress
}

h3_destroy()
{
	tc qdisc del dev $h3 ingress

	ip route del 2001:db8:2::/64 vrf v$h3
	ip route del 2001:db8:1::/64 vrf v$h3

	ip route del 198.51.100.16/28 vrf v$h3
	ip route del 198.51.100.0/28 vrf v$h3

	simple_if_fini $h3 198.51.100.34/28 2001:db8:3::2/64
}

router_create()
{
	ip link set dev $rp1 up
	ip link set dev $rp2 up
	ip link set dev $rp3 up

	ip address add 198.51.100.1/28 dev $rp1
	ip address add 198.51.100.17/28 dev $rp2
	ip address add 198.51.100.33/28 dev $rp3

	ip address add 2001:db8:1::1/64 dev $rp1
	ip address add 2001:db8:2::1/64 dev $rp2
	ip address add 2001:db8:3::1/64 dev $rp3

	tc qdisc add dev $rp3 ingress
}

router_destroy()
{
	tc qdisc del dev $rp3 ingress

	ip address del 2001:db8:3::1/64 dev $rp3
	ip address del 2001:db8:2::1/64 dev $rp2
	ip address del 2001:db8:1::1/64 dev $rp1

	ip address del 198.51.100.33/28 dev $rp3
	ip address del 198.51.100.17/28 dev $rp2
	ip address del 198.51.100.1/28 dev $rp1

	ip link set dev $rp3 down
	ip link set dev $rp2 down
	ip link set dev $rp1 down
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	rp1=${NETIFS[p2]}

	rp2=${NETIFS[p3]}
	h2=${NETIFS[p4]}

	rp3=${NETIFS[p5]}
	h3=${NETIFS[p6]}

	adf_mcd_start || exit "$EXIT_STATUS"

	vrf_prepare

	h1_create
	h2_create
	h3_create

	router_create

	forwarding_enable
}

cleanup()
{
	pre_cleanup

	forwarding_restore

	router_destroy

	h3_destroy
	h2_destroy
	h1_destroy

	vrf_cleanup

	defer_scopes_cleanup
}

create_mcast_sg()
{
	local if_name=$1; shift
	local s_addr=$1; shift
	local mcast=$1; shift
	local dest_ifs=("${@}")

	mc_cli add "$if_name" "$s_addr" "$mcast" "${dest_ifs[@]}"
}

delete_mcast_sg()
{
	local if_name=$1; shift
	local s_addr=$1; shift
	local mcast=$1; shift
	local dest_ifs=("${@}")

        mc_cli remove "$if_name" "$s_addr" "$mcast" "${dest_ifs[@]}"
}

mcast_v4()
{
	# Add two interfaces to an MC group, send a packet to the MC group and
	# verify packets are received on both. Then delete the route and verify
	# packets are no longer received.

	RET=0

	tc filter add dev $h2 ingress protocol ip pref 1 handle 122 flower \
		dst_ip 225.1.2.3 action drop
	tc filter add dev $h3 ingress protocol ip pref 1 handle 133 flower \
		dst_ip 225.1.2.3 action drop

	create_mcast_sg $rp1 198.51.100.2 225.1.2.3 $rp2 $rp3

	# Send frames with the corresponding L2 destination address.
	$MZ $h1 -c 5 -p 128 -t udp -a 00:11:22:33:44:55 -b 01:00:5e:01:02:03 \
		-A 198.51.100.2 -B 225.1.2.3 -q

	tc_check_packets "dev $h2 ingress" 122 5
	check_err $? "Multicast not received on first host"
	tc_check_packets "dev $h3 ingress" 133 5
	check_err $? "Multicast not received on second host"

	delete_mcast_sg $rp1 198.51.100.2 225.1.2.3 $rp2 $rp3

	$MZ $h1 -c 5 -p 128 -t udp -a 00:11:22:33:44:55 -b 01:00:5e:01:02:03 \
		-A 198.51.100.2 -B 225.1.2.3 -q

	tc_check_packets "dev $h2 ingress" 122 5
	check_err $? "Multicast received on host although deleted"
	tc_check_packets "dev $h3 ingress" 133 5
	check_err $? "Multicast received on second host although deleted"

	tc filter del dev $h3 ingress protocol ip pref 1 handle 133 flower
	tc filter del dev $h2 ingress protocol ip pref 1 handle 122 flower

	log_test "mcast IPv4"
}

mcast_v6()
{
	# Add two interfaces to an MC group, send a packet to the MC group and
	# verify packets are received on both. Then delete the route and verify
	# packets are no longer received.

	RET=0

	tc filter add dev $h2 ingress protocol ipv6 pref 1 handle 122 flower \
		dst_ip ff0e::3 action drop
	tc filter add dev $h3 ingress protocol ipv6 pref 1 handle 133 flower \
		dst_ip ff0e::3 action drop

	create_mcast_sg $rp1 2001:db8:1::2 ff0e::3 $rp2 $rp3

	# Send frames with the corresponding L2 destination address.
	$MZ $h1 -6 -c 5 -p 128 -t udp -a 00:11:22:33:44:55 \
		-b 33:33:00:00:00:03 -A 2001:db8:1::2 -B ff0e::3 -q

	tc_check_packets "dev $h2 ingress" 122 5
	check_err $? "Multicast not received on first host"
	tc_check_packets "dev $h3 ingress" 133 5
	check_err $? "Multicast not received on second host"

	delete_mcast_sg $rp1 2001:db8:1::2 ff0e::3 $rp2 $rp3

	$MZ $h1 -6 -c 5 -p 128 -t udp -a 00:11:22:33:44:55 \
		-b 33:33:00:00:00:03 -A 2001:db8:1::2 -B ff0e::3 -q

	tc_check_packets "dev $h2 ingress" 122 5
	check_err $? "Multicast received on first host although deleted"
	tc_check_packets "dev $h3 ingress" 133 5
	check_err $? "Multicast received on second host although deleted"

	tc filter del dev $h3 ingress protocol ipv6 pref 1 handle 133 flower
	tc filter del dev $h2 ingress protocol ipv6 pref 1 handle 122 flower

	log_test "mcast IPv6"
}

rpf_v4()
{
	# Add a multicast route from first router port to the other two. Send
	# matching packets and test that both hosts receive them. Then, send
	# the same packets via the third router port and test that they do not
	# reach any host due to RPF check. A filter with 'skip_hw' is added to
	# test that devices capable of multicast routing offload trap those
	# packets. The filter is essentialy a NOP in other scenarios.

	RET=0

	tc filter add dev $h1 ingress protocol ip pref 1 handle 1 flower \
		dst_ip 225.1.2.3 ip_proto udp dst_port 12345 action drop
	tc filter add dev $h2 ingress protocol ip pref 1 handle 1 flower \
		dst_ip 225.1.2.3 ip_proto udp dst_port 12345 action drop
	tc filter add dev $h3 ingress protocol ip pref 1 handle 1 flower \
		dst_ip 225.1.2.3 ip_proto udp dst_port 12345 action drop
	tc filter add dev $rp3 ingress protocol ip pref 1 handle 1 flower \
		skip_hw dst_ip 225.1.2.3 ip_proto udp dst_port 12345 action pass

	create_mcast_sg $rp1 198.51.100.2 225.1.2.3 $rp2 $rp3

	$MZ $h1 -c 5 -p 128 -t udp "ttl=10,sp=54321,dp=12345" \
		-a 00:11:22:33:44:55 -b 01:00:5e:01:02:03 \
		-A 198.51.100.2 -B 225.1.2.3 -q

	tc_check_packets "dev $h2 ingress" 1 5
	check_err $? "Multicast not received on first host"
	tc_check_packets "dev $h3 ingress" 1 5
	check_err $? "Multicast not received on second host"

	$MZ $h3 -c 5 -p 128 -t udp "ttl=10,sp=54321,dp=12345" \
		-a 00:11:22:33:44:55 -b 01:00:5e:01:02:03 \
		-A 198.51.100.2 -B 225.1.2.3 -q

	tc_check_packets "dev $h1 ingress" 1 0
	check_err $? "Multicast received on first host when should not"
	tc_check_packets "dev $h2 ingress" 1 5
	check_err $? "Multicast received on second host when should not"
	tc_check_packets "dev $rp3 ingress" 1 5
	check_err $? "Packets not trapped due to RPF check"

	delete_mcast_sg $rp1 198.51.100.2 225.1.2.3 $rp2 $rp3

	tc filter del dev $rp3 ingress protocol ip pref 1 handle 1 flower
	tc filter del dev $h3 ingress protocol ip pref 1 handle 1 flower
	tc filter del dev $h2 ingress protocol ip pref 1 handle 1 flower
	tc filter del dev $h1 ingress protocol ip pref 1 handle 1 flower

	log_test "RPF IPv4"
}

rpf_v6()
{
	RET=0

	tc filter add dev $h1 ingress protocol ipv6 pref 1 handle 1 flower \
		dst_ip ff0e::3 ip_proto udp dst_port 12345 action drop
	tc filter add dev $h2 ingress protocol ipv6 pref 1 handle 1 flower \
		dst_ip ff0e::3 ip_proto udp dst_port 12345 action drop
	tc filter add dev $h3 ingress protocol ipv6 pref 1 handle 1 flower \
		dst_ip ff0e::3 ip_proto udp dst_port 12345 action drop
	tc filter add dev $rp3 ingress protocol ipv6 pref 1 handle 1 flower \
		skip_hw dst_ip ff0e::3 ip_proto udp dst_port 12345 action pass

	create_mcast_sg $rp1 2001:db8:1::2 ff0e::3 $rp2 $rp3

	$MZ $h1 -6 -c 5 -p 128 -t udp "ttl=10,sp=54321,dp=12345" \
		-a 00:11:22:33:44:55 -b 33:33:00:00:00:03 \
		-A 2001:db8:1::2 -B ff0e::3 -q

	tc_check_packets "dev $h2 ingress" 1 5
	check_err $? "Multicast not received on first host"
	tc_check_packets "dev $h3 ingress" 1 5
	check_err $? "Multicast not received on second host"

	$MZ $h3 -6 -c 5 -p 128 -t udp "ttl=10,sp=54321,dp=12345" \
		-a 00:11:22:33:44:55 -b 33:33:00:00:00:03 \
		-A 2001:db8:1::2 -B ff0e::3 -q

	tc_check_packets "dev $h1 ingress" 1 0
	check_err $? "Multicast received on first host when should not"
	tc_check_packets "dev $h2 ingress" 1 5
	check_err $? "Multicast received on second host when should not"
	tc_check_packets "dev $rp3 ingress" 1 5
	check_err $? "Packets not trapped due to RPF check"

	delete_mcast_sg $rp1 2001:db8:1::2 ff0e::3 $rp2 $rp3

	tc filter del dev $rp3 ingress protocol ipv6 pref 1 handle 1 flower
	tc filter del dev $h3 ingress protocol ipv6 pref 1 handle 1 flower
	tc filter del dev $h2 ingress protocol ipv6 pref 1 handle 1 flower
	tc filter del dev $h1 ingress protocol ipv6 pref 1 handle 1 flower

	log_test "RPF IPv6"
}

unres_v4()
{
	# Send a multicast packet not corresponding to an installed route,
	# causing the kernel to queue the packet for resolution and emit an
	# IGMPMSG_NOCACHE notification. smcrouted will react to this
	# notification by consulting its (*, G) list and installing an (S, G)
	# route, which will be used to forward the queued packet.

	RET=0

	tc filter add dev $h2 ingress protocol ip pref 1 handle 1 flower \
		dst_ip 225.1.2.3 ip_proto udp dst_port 12345 action drop
	tc filter add dev $h3 ingress protocol ip pref 1 handle 1 flower \
		dst_ip 225.1.2.3 ip_proto udp dst_port 12345 action drop

	# Forwarding should fail before installing a matching (*, G).
	$MZ $h1 -c 1 -p 128 -t udp "ttl=10,sp=54321,dp=12345" \
		-a 00:11:22:33:44:55 -b 01:00:5e:01:02:03 \
		-A 198.51.100.2 -B 225.1.2.3 -q

	tc_check_packets "dev $h2 ingress" 1 0
	check_err $? "Multicast received on first host when should not"
	tc_check_packets "dev $h3 ingress" 1 0
	check_err $? "Multicast received on second host when should not"

	# Create (*, G). Will not be installed in the kernel.
	create_mcast_sg $rp1 0.0.0.0 225.1.2.3 $rp2 $rp3

	$MZ $h1 -c 1 -p 128 -t udp "ttl=10,sp=54321,dp=12345" \
		-a 00:11:22:33:44:55 -b 01:00:5e:01:02:03 \
		-A 198.51.100.2 -B 225.1.2.3 -q

	tc_check_packets "dev $h2 ingress" 1 1
	check_err $? "Multicast not received on first host"
	tc_check_packets "dev $h3 ingress" 1 1
	check_err $? "Multicast not received on second host"

	delete_mcast_sg $rp1 0.0.0.0 225.1.2.3 $rp2 $rp3

	tc filter del dev $h3 ingress protocol ip pref 1 handle 1 flower
	tc filter del dev $h2 ingress protocol ip pref 1 handle 1 flower

	log_test "Unresolved queue IPv4"
}

unres_v6()
{
	# Send a multicast packet not corresponding to an installed route,
	# causing the kernel to queue the packet for resolution and emit an
	# MRT6MSG_NOCACHE notification. smcrouted will react to this
	# notification by consulting its (*, G) list and installing an (S, G)
	# route, which will be used to forward the queued packet.

	RET=0

	tc filter add dev $h2 ingress protocol ipv6 pref 1 handle 1 flower \
		dst_ip ff0e::3 ip_proto udp dst_port 12345 action drop
	tc filter add dev $h3 ingress protocol ipv6 pref 1 handle 1 flower \
		dst_ip ff0e::3 ip_proto udp dst_port 12345 action drop

	# Forwarding should fail before installing a matching (*, G).
	$MZ $h1 -6 -c 1 -p 128 -t udp "ttl=10,sp=54321,dp=12345" \
		-a 00:11:22:33:44:55 -b 33:33:00:00:00:03 \
		-A 2001:db8:1::2 -B ff0e::3 -q

	tc_check_packets "dev $h2 ingress" 1 0
	check_err $? "Multicast received on first host when should not"
	tc_check_packets "dev $h3 ingress" 1 0
	check_err $? "Multicast received on second host when should not"

	# Create (*, G). Will not be installed in the kernel.
	create_mcast_sg $rp1 :: ff0e::3 $rp2 $rp3

	$MZ $h1 -6 -c 1 -p 128 -t udp "ttl=10,sp=54321,dp=12345" \
		-a 00:11:22:33:44:55 -b 33:33:00:00:00:03 \
		-A 2001:db8:1::2 -B ff0e::3 -q

	tc_check_packets "dev $h2 ingress" 1 1
	check_err $? "Multicast not received on first host"
	tc_check_packets "dev $h3 ingress" 1 1
	check_err $? "Multicast not received on second host"

	delete_mcast_sg $rp1 :: ff0e::3 $rp2 $rp3

	tc filter del dev $h3 ingress protocol ipv6 pref 1 handle 1 flower
	tc filter del dev $h2 ingress protocol ipv6 pref 1 handle 1 flower

	log_test "Unresolved queue IPv6"
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS
