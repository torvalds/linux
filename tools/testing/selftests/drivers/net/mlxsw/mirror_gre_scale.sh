# SPDX-License-Identifier: GPL-2.0

# Test offloading a number of mirrors-to-gretap. The test creates a number of
# tunnels. Then it adds one flower mirror for each of the tunnels, matching a
# given host IP. Then it generates traffic at each of the host IPs and checks
# that the traffic has been mirrored at the appropriate tunnel.
#
#   +--------------------------+                   +--------------------------+
#   | H1                       |                   |                       H2 |
#   |     + $h1                |                   |                $h2 +     |
#   |     | 2001:db8:1:X::1/64 |                   | 2001:db8:1:X::2/64 |     |
#   +-----|--------------------+                   +--------------------|-----+
#         |                                                             |
#   +-----|-------------------------------------------------------------|-----+
#   | SW  o--> mirrors                                                  |     |
#   | +---|-------------------------------------------------------------|---+ |
#   | |   + $swp1                    BR                           $swp2 +   | |
#   | +---------------------------------------------------------------------+ |
#   |                                                                         |
#   |     + $swp3                          + gt6-<X> (ip6gretap)              |
#   |     | 2001:db8:2:X::1/64             : loc=2001:db8:2:X::1              |
#   |     |                                : rem=2001:db8:2:X::2              |
#   |     |                                : ttl=100                          |
#   |     |                                : tos=inherit                      |
#   |     |                                :                                  |
#   +-----|--------------------------------:----------------------------------+
#         |                                :
#   +-----|--------------------------------:----------------------------------+
#   | H3  + $h3                            + h3-gt6-<X> (ip6gretap)           |
#   |       2001:db8:2:X::2/64               loc=2001:db8:2:X::2              |
#   |                                        rem=2001:db8:2:X::1              |
#   |                                        ttl=100                          |
#   |                                        tos=inherit                      |
#   |                                                                         |
#   +-------------------------------------------------------------------------+

source ../../../../net/forwarding/mirror_lib.sh

MIRROR_NUM_NETIFS=6

mirror_gre_ipv6_addr()
{
	local net=$1; shift
	local num=$1; shift

	printf "2001:db8:%x:%x" $net $num
}

mirror_gre_tunnels_create()
{
	local count=$1; shift
	local should_fail=$1; shift

	MIRROR_GRE_BATCH_FILE="$(mktemp)"
	for ((i=0; i < count; ++i)); do
		local match_dip=$(mirror_gre_ipv6_addr 1 $i)::2
		local htun=h3-gt6-$i
		local tun=gt6-$i

		((mirror_gre_tunnels++))

		ip address add dev $h1 $(mirror_gre_ipv6_addr 1 $i)::1/64
		ip address add dev $h2 $(mirror_gre_ipv6_addr 1 $i)::2/64

		ip address add dev $swp3 $(mirror_gre_ipv6_addr 2 $i)::1/64
		ip address add dev $h3 $(mirror_gre_ipv6_addr 2 $i)::2/64

		tunnel_create $tun ip6gretap \
			      $(mirror_gre_ipv6_addr 2 $i)::1 \
			      $(mirror_gre_ipv6_addr 2 $i)::2 \
			      ttl 100 tos inherit allow-localremote

		tunnel_create $htun ip6gretap \
			      $(mirror_gre_ipv6_addr 2 $i)::2 \
			      $(mirror_gre_ipv6_addr 2 $i)::1
		ip link set $htun vrf v$h3
		matchall_sink_create $htun

		cat >> $MIRROR_GRE_BATCH_FILE <<-EOF
			filter add dev $swp1 ingress pref 1000 \
				protocol ipv6 \
				flower skip_sw dst_ip $match_dip \
				action mirred egress mirror dev $tun
		EOF
	done

	tc -b $MIRROR_GRE_BATCH_FILE
	check_err_fail $should_fail $? "Mirror rule insertion"
}

mirror_gre_tunnels_destroy()
{
	local count=$1; shift

	for ((i=0; i < count; ++i)); do
		local htun=h3-gt6-$i
		local tun=gt6-$i

		ip address del dev $h3 $(mirror_gre_ipv6_addr 2 $i)::2/64
		ip address del dev $swp3 $(mirror_gre_ipv6_addr 2 $i)::1/64

		ip address del dev $h2 $(mirror_gre_ipv6_addr 1 $i)::2/64
		ip address del dev $h1 $(mirror_gre_ipv6_addr 1 $i)::1/64

		tunnel_destroy $htun
		tunnel_destroy $tun
	done
}

mirror_gre_test()
{
	local count=$1; shift
	local should_fail=$1; shift

	mirror_gre_tunnels_create $count $should_fail
	if ((should_fail)); then
	    return
	fi

	sleep 5

	for ((i = 0; i < count; ++i)); do
		local sip=$(mirror_gre_ipv6_addr 1 $i)::1
		local dip=$(mirror_gre_ipv6_addr 1 $i)::2
		local htun=h3-gt6-$i
		local message

		icmp6_capture_install $htun
		mirror_test v$h1 $sip $dip $htun 100 10
		icmp6_capture_uninstall $htun
	done
}

mirror_gre_setup_prepare()
{
	h1=${NETIFS[p1]}
	swp1=${NETIFS[p2]}

	swp2=${NETIFS[p3]}
	h2=${NETIFS[p4]}

	swp3=${NETIFS[p5]}
	h3=${NETIFS[p6]}

	mirror_gre_tunnels=0

	vrf_prepare

	simple_if_init $h1
	simple_if_init $h2
	simple_if_init $h3

	ip link add name br1 type bridge vlan_filtering 1
	ip link set dev br1 addrgenmode none
	ip link set dev br1 up

	ip link set dev $swp1 master br1
	ip link set dev $swp1 up
	tc qdisc add dev $swp1 clsact

	ip link set dev $swp2 master br1
	ip link set dev $swp2 up

	ip link set dev $swp3 up
}

mirror_gre_cleanup()
{
	mirror_gre_tunnels_destroy $mirror_gre_tunnels

	ip link set dev $swp3 down

	ip link set dev $swp2 down

	tc qdisc del dev $swp1 clsact
	ip link set dev $swp1 down

	ip link del dev br1

	simple_if_fini $h3
	simple_if_fini $h2
	simple_if_fini $h1

	vrf_cleanup
}
