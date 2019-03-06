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

ALL_TESTS="mcast_v4 mcast_v6"
NUM_NETIFS=6
source lib.sh
source tc_common.sh

require_command $MCD
require_command $MC_CLI
table_name=selftests

h1_create()
{
	simple_if_init $h1 198.51.100.2/28 2001:db8:1::2/64

	ip route add 198.51.100.16/28 vrf v$h1 nexthop via 198.51.100.1
	ip route add 198.51.100.32/28 vrf v$h1 nexthop via 198.51.100.1

	ip route add 2001:db8:2::/64 vrf v$h1 nexthop via 2001:db8:1::1
	ip route add 2001:db8:3::/64 vrf v$h1 nexthop via 2001:db8:1::1
}

h1_destroy()
{
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
}

router_destroy()
{
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

start_mcd()
{
	SMCROUTEDIR="$(mktemp -d)"

	for ((i = 1; i <= $NUM_NETIFS; ++i)); do
		echo "phyint ${NETIFS[p$i]} enable" >> \
			$SMCROUTEDIR/$table_name.conf
	done

	$MCD -N -I $table_name -f $SMCROUTEDIR/$table_name.conf \
		-P $SMCROUTEDIR/$table_name.pid
}

kill_mcd()
{
	pkill $MCD
	rm -rf $SMCROUTEDIR
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	rp1=${NETIFS[p2]}

	rp2=${NETIFS[p3]}
	h2=${NETIFS[p4]}

	rp3=${NETIFS[p5]}
	h3=${NETIFS[p6]}

	start_mcd

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

	kill_mcd
}

create_mcast_sg()
{
	local if_name=$1; shift
	local s_addr=$1; shift
	local mcast=$1; shift
	local dest_ifs=${@}

	$MC_CLI -I $table_name add $if_name $s_addr $mcast $dest_ifs
}

delete_mcast_sg()
{
	local if_name=$1; shift
	local s_addr=$1; shift
	local mcast=$1; shift
	local dest_ifs=${@}

        $MC_CLI -I $table_name remove $if_name $s_addr $mcast $dest_ifs
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

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS
