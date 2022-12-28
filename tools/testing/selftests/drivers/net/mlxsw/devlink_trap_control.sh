#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test devlink-trap control trap functionality over mlxsw. Each registered
# control packet trap is tested to make sure it is triggered under the right
# conditions.
#
# +---------------------------------+
# | H1 (vrf)                        |
# |    + $h1                        |
# |    | 192.0.2.1/24               |
# |    | 2001:db8:1::1/64           |
# |    |                            |
# |    |  default via 192.0.2.2     |
# |    |  default via 2001:db8:1::2 |
# +----|----------------------------+
#      |
# +----|----------------------------------------------------------------------+
# | SW |                                                                      |
# |    + $rp1                                                                 |
# |        192.0.2.2/24                                                       |
# |        2001:db8:1::2/64                                                   |
# |                                                                           |
# |        2001:db8:2::2/64                                                   |
# |        198.51.100.2/24                                                    |
# |    + $rp2                                                                 |
# |    |                                                                      |
# +----|----------------------------------------------------------------------+
#      |
# +----|----------------------------+
# |    |  default via 198.51.100.2  |
# |    |  default via 2001:db8:2::2 |
# |    |                            |
# |    | 2001:db8:2::1/64           |
# |    | 198.51.100.1/24            |
# |    + $h2                        |
# | H2 (vrf)                        |
# +---------------------------------+

lib_dir=$(dirname $0)/../../../net/forwarding

ALL_TESTS="
	stp_test
	lacp_test
	lldp_test
	igmp_query_test
	igmp_v1_report_test
	igmp_v2_report_test
	igmp_v3_report_test
	igmp_v2_leave_test
	mld_query_test
	mld_v1_report_test
	mld_v2_report_test
	mld_v1_done_test
	ipv4_dhcp_test
	ipv6_dhcp_test
	arp_request_test
	arp_response_test
	ipv6_neigh_solicit_test
	ipv6_neigh_advert_test
	ipv4_bfd_test
	ipv6_bfd_test
	ipv4_ospf_test
	ipv6_ospf_test
	ipv4_bgp_test
	ipv6_bgp_test
	ipv4_vrrp_test
	ipv6_vrrp_test
	ipv4_pim_test
	ipv6_pim_test
	uc_loopback_test
	local_route_test
	external_route_test
	ipv6_uc_dip_link_local_scope_test
	ipv4_router_alert_test
	ipv6_router_alert_test
	ipv6_dip_all_nodes_test
	ipv6_dip_all_routers_test
	ipv6_router_solicit_test
	ipv6_router_advert_test
	ipv6_redirect_test
	ptp_event_test
	ptp_general_test
	flow_action_sample_test
	flow_action_trap_test
	eapol_test
"
NUM_NETIFS=4
source $lib_dir/lib.sh
source $lib_dir/devlink_lib.sh
source mlxsw_lib.sh

h1_create()
{
	simple_if_init $h1 192.0.2.1/24 2001:db8:1::1/64

	ip -4 route add default vrf v$h1 nexthop via 192.0.2.2
	ip -6 route add default vrf v$h1 nexthop via 2001:db8:1::2
}

h1_destroy()
{
	ip -6 route del default vrf v$h1 nexthop via 2001:db8:1::2
	ip -4 route del default vrf v$h1 nexthop via 192.0.2.2

	simple_if_fini $h1 192.0.2.1/24 2001:db8:1::1/64
}

h2_create()
{
	simple_if_init $h2 198.51.100.1/24 2001:db8:2::1/64

	ip -4 route add default vrf v$h2 nexthop via 198.51.100.2
	ip -6 route add default vrf v$h2 nexthop via 2001:db8:2::2
}

h2_destroy()
{
	ip -6 route del default vrf v$h2 nexthop via 2001:db8:2::2
	ip -4 route del default vrf v$h2 nexthop via 198.51.100.2

	simple_if_fini $h2 198.51.100.1/24 2001:db8:2::1/64
}

router_create()
{
	ip link set dev $rp1 up
	ip link set dev $rp2 up

	__addr_add_del $rp1 add 192.0.2.2/24 2001:db8:1::2/64
	__addr_add_del $rp2 add 198.51.100.2/24 2001:db8:2::2/64
}

router_destroy()
{
	__addr_add_del $rp2 del 198.51.100.2/24 2001:db8:2::2/64
	__addr_add_del $rp1 del 192.0.2.2/24 2001:db8:1::2/64

	ip link set dev $rp2 down
	ip link set dev $rp1 down
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	rp1=${NETIFS[p2]}

	rp2=${NETIFS[p3]}
	h2=${NETIFS[p4]}

	vrf_prepare
	forwarding_enable

	h1_create
	h2_create
	router_create
}

cleanup()
{
	pre_cleanup

	router_destroy
	h2_destroy
	h1_destroy

	forwarding_restore
	vrf_cleanup
}

stp_test()
{
	devlink_trap_stats_test "STP" "stp" $MZ $h1 -c 1 -t bpdu -q
}

lacp_payload_get()
{
	local source_mac=$1; shift
	local p

	p=$(:
		)"01:80:C2:00:00:02:"$(       : ETH daddr
		)"$source_mac:"$(             : ETH saddr
		)"88:09:"$(                   : ETH type
		)
	echo $p
}

lacp_test()
{
	local h1mac=$(mac_get $h1)

	devlink_trap_stats_test "LACP" "lacp" $MZ $h1 -c 1 \
		$(lacp_payload_get $h1mac) -p 100 -q
}

lldp_payload_get()
{
	local source_mac=$1; shift
	local p

	p=$(:
		)"01:80:C2:00:00:0E:"$(       : ETH daddr
		)"$source_mac:"$(             : ETH saddr
		)"88:CC:"$(                   : ETH type
		)
	echo $p
}

lldp_test()
{
	local h1mac=$(mac_get $h1)

	devlink_trap_stats_test "LLDP" "lldp" $MZ $h1 -c 1 \
		$(lldp_payload_get $h1mac) -p 100 -q
}

igmp_query_test()
{
	# IGMP (IP Protocol 2) Membership Query (Type 0x11)
	devlink_trap_stats_test "IGMP Membership Query" "igmp_query" \
		$MZ $h1 -c 1 -a own -b 01:00:5E:00:00:01 \
		-A 192.0.2.1 -B 224.0.0.1 -t ip proto=2,p=11 -p 100 -q
}

igmp_v1_report_test()
{
	# IGMP (IP Protocol 2) Version 1 Membership Report (Type 0x12)
	devlink_trap_stats_test "IGMP Version 1 Membership Report" \
		"igmp_v1_report" $MZ $h1 -c 1 -a own -b 01:00:5E:00:00:01 \
		-A 192.0.2.1 -B 244.0.0.1 -t ip proto=2,p=12 -p 100 -q
}

igmp_v2_report_test()
{
	# IGMP (IP Protocol 2) Version 2 Membership Report (Type 0x16)
	devlink_trap_stats_test "IGMP Version 2 Membership Report" \
		"igmp_v2_report" $MZ $h1 -c 1 -a own -b 01:00:5E:00:00:01 \
		-A 192.0.2.1 -B 244.0.0.1 -t ip proto=2,p=16 -p 100 -q
}

igmp_v3_report_test()
{
	# IGMP (IP Protocol 2) Version 3 Membership Report (Type 0x22)
	devlink_trap_stats_test "IGMP Version 3 Membership Report" \
		"igmp_v3_report" $MZ $h1 -c 1 -a own -b 01:00:5E:00:00:01 \
		-A 192.0.2.1 -B 244.0.0.1 -t ip proto=2,p=22 -p 100 -q
}

igmp_v2_leave_test()
{
	# IGMP (IP Protocol 2) Version 2 Leave Group (Type 0x17)
	devlink_trap_stats_test "IGMP Version 2 Leave Group" \
		"igmp_v2_leave" $MZ $h1 -c 1 -a own -b 01:00:5E:00:00:02 \
		-A 192.0.2.1 -B 224.0.0.2 -t ip proto=2,p=17 -p 100 -q
}

mld_payload_get()
{
	local type=$1; shift
	local p

	type=$(printf "%x" $type)
	p=$(:
		)"3A:"$(			: Next Header - ICMPv6
		)"00:"$(			: Hdr Ext Len
		)"00:00:00:00:00:00:"$(		: Options and Padding
		)"$type:"$(			: ICMPv6.type
		)"00:"$(			: ICMPv6.code
		)"00:"$(			: ICMPv6.checksum
		)
	echo $p
}

mld_query_test()
{
	# MLD Multicast Listener Query (Type 130)
	devlink_trap_stats_test "MLD Multicast Listener Query" "mld_query" \
		$MZ $h1 -6 -c 1 -A fe80::1 -B ff02::1 \
		-t ip hop=1,next=0,payload=$(mld_payload_get 130) -p 100 -q
}

mld_v1_report_test()
{
	# MLD Version 1 Multicast Listener Report (Type 131)
	devlink_trap_stats_test "MLD Version 1 Multicast Listener Report" \
		"mld_v1_report" $MZ $h1 -6 -c 1 -A fe80::1 -B ff02::16 \
		-t ip hop=1,next=0,payload=$(mld_payload_get 131) -p 100 -q
}

mld_v2_report_test()
{
	# MLD Version 2 Multicast Listener Report (Type 143)
	devlink_trap_stats_test "MLD Version 2 Multicast Listener Report" \
		"mld_v2_report" $MZ $h1 -6 -c 1 -A fe80::1 -B ff02::16 \
		-t ip hop=1,next=0,payload=$(mld_payload_get 143) -p 100 -q
}

mld_v1_done_test()
{
	# MLD Version 1 Multicast Listener Done (Type 132)
	devlink_trap_stats_test "MLD Version 1 Multicast Listener Done" \
		"mld_v1_done" $MZ $h1 -6 -c 1 -A fe80::1 -B ff02::16 \
		-t ip hop=1,next=0,payload=$(mld_payload_get 132) -p 100 -q
}

ipv4_dhcp_test()
{
	devlink_trap_stats_test "IPv4 DHCP Port 67" "ipv4_dhcp" \
		$MZ $h1 -c 1 -a own -b bcast -A 0.0.0.0 -B 255.255.255.255 \
		-t udp sp=68,dp=67 -p 100 -q

	devlink_trap_stats_test "IPv4 DHCP Port 68" "ipv4_dhcp" \
		$MZ $h1 -c 1 -a own -b $(mac_get $rp1) -A 192.0.2.1 \
		-B 255.255.255.255 -t udp sp=67,dp=68 -p 100 -q
}

ipv6_dhcp_test()
{
	devlink_trap_stats_test "IPv6 DHCP Port 547" "ipv6_dhcp" \
		$MZ $h1 -6 -c 1 -A fe80::1 -B ff02::1:2 -t udp sp=546,dp=547 \
		-p 100 -q

	devlink_trap_stats_test "IPv6 DHCP Port 546" "ipv6_dhcp" \
		$MZ $h1 -6 -c 1 -A fe80::1 -B ff02::1:2 -t udp sp=547,dp=546 \
		-p 100 -q
}

arp_request_test()
{
	devlink_trap_stats_test "ARP Request" "arp_request" \
		$MZ $h1 -c 1 -a own -b bcast -t arp request -p 100 -q
}

arp_response_test()
{
	devlink_trap_stats_test "ARP Response" "arp_response" \
		$MZ $h1 -c 1 -a own -b $(mac_get $rp1) -t arp reply -p 100 -q
}

icmpv6_header_get()
{
	local type=$1; shift
	local p

	type=$(printf "%x" $type)
	p=$(:
		)"$type:"$(			: ICMPv6.type
		)"00:"$(			: ICMPv6.code
		)"00:"$(			: ICMPv6.checksum
		)
	echo $p
}

ipv6_neigh_solicit_test()
{
	devlink_trap_stats_test "IPv6 Neighbour Solicitation" \
		"ipv6_neigh_solicit" $MZ $h1 -6 -c 1 \
		-A fe80::1 -B ff02::1:ff00:02 \
		-t ip hop=1,next=58,payload=$(icmpv6_header_get 135) -p 100 -q
}

ipv6_neigh_advert_test()
{
	devlink_trap_stats_test "IPv6 Neighbour Advertisement" \
		"ipv6_neigh_advert" $MZ $h1 -6 -c 1 -a own -b $(mac_get $rp1) \
		-A fe80::1 -B 2001:db8:1::2 \
		-t ip hop=1,next=58,payload=$(icmpv6_header_get 136) -p 100 -q
}

ipv4_bfd_test()
{
	devlink_trap_stats_test "IPv4 BFD Control - Port 3784" "ipv4_bfd" \
		$MZ $h1 -c 1 -a own -b $(mac_get $rp1) \
		-A 192.0.2.1 -B 192.0.2.2 -t udp sp=49153,dp=3784 -p 100 -q

	devlink_trap_stats_test "IPv4 BFD Echo - Port 3785" "ipv4_bfd" \
		$MZ $h1 -c 1 -a own -b $(mac_get $rp1) \
		-A 192.0.2.1 -B 192.0.2.2 -t udp sp=49153,dp=3785 -p 100 -q
}

ipv6_bfd_test()
{
	devlink_trap_stats_test "IPv6 BFD Control - Port 3784" "ipv6_bfd" \
		$MZ $h1 -6 -c 1 -a own -b $(mac_get $rp1) \
		-A 2001:db8:1::1 -B 2001:db8:1::2 \
		-t udp sp=49153,dp=3784 -p 100 -q

	devlink_trap_stats_test "IPv6 BFD Echo - Port 3785" "ipv6_bfd" \
		$MZ $h1 -6 -c 1 -a own -b $(mac_get $rp1) \
		-A 2001:db8:1::1 -B 2001:db8:1::2 \
		-t udp sp=49153,dp=3785 -p 100 -q
}

ipv4_ospf_test()
{
	devlink_trap_stats_test "IPv4 OSPF - Multicast" "ipv4_ospf" \
		$MZ $h1 -c 1 -a own -b 01:00:5e:00:00:05 \
		-A 192.0.2.1 -B 224.0.0.5 -t ip proto=89 -p 100 -q

	devlink_trap_stats_test "IPv4 OSPF - Unicast" "ipv4_ospf" \
		$MZ $h1 -c 1 -a own -b $(mac_get $rp1) \
		-A 192.0.2.1 -B 192.0.2.2 -t ip proto=89 -p 100 -q
}

ipv6_ospf_test()
{
	devlink_trap_stats_test "IPv6 OSPF - Multicast" "ipv6_ospf" \
		$MZ $h1 -6 -c 1 -a own -b 33:33:00:00:00:05 \
		-A fe80::1 -B ff02::5 -t ip next=89 -p 100 -q

	devlink_trap_stats_test "IPv6 OSPF - Unicast" "ipv6_ospf" \
		$MZ $h1 -6 -c 1 -a own -b $(mac_get $rp1) \
		-A 2001:db8:1::1 -B 2001:db8:1::2 -t ip next=89 -p 100 -q
}

ipv4_bgp_test()
{
	devlink_trap_stats_test "IPv4 BGP" "ipv4_bgp" \
		$MZ $h1 -c 1 -a own -b $(mac_get $rp1) \
		-A 192.0.2.1 -B 192.0.2.2 -t tcp sp=54321,dp=179,flags=rst \
		-p 100 -q
}

ipv6_bgp_test()
{
	devlink_trap_stats_test "IPv6 BGP" "ipv6_bgp" \
		$MZ $h1 -6 -c 1 -a own -b $(mac_get $rp1) \
		-A 2001:db8:1::1 -B 2001:db8:1::2 \
		-t tcp sp=54321,dp=179,flags=rst -p 100 -q
}

ipv4_vrrp_test()
{
	devlink_trap_stats_test "IPv4 VRRP" "ipv4_vrrp" \
		$MZ $h1 -c 1 -a own -b 01:00:5e:00:00:12 \
		-A 192.0.2.1 -B 224.0.0.18 -t ip proto=112 -p 100 -q
}

ipv6_vrrp_test()
{
	devlink_trap_stats_test "IPv6 VRRP" "ipv6_vrrp" \
		$MZ $h1 -6 -c 1 -a own -b 33:33:00:00:00:12 \
		-A fe80::1 -B ff02::12 -t ip next=112 -p 100 -q
}

ipv4_pim_test()
{
	devlink_trap_stats_test "IPv4 PIM - Multicast" "ipv4_pim" \
		$MZ $h1 -c 1 -a own -b 01:00:5e:00:00:0d \
		-A 192.0.2.1 -B 224.0.0.13 -t ip proto=103 -p 100 -q

	devlink_trap_stats_test "IPv4 PIM - Unicast" "ipv4_pim" \
		$MZ $h1 -c 1 -a own -b $(mac_get $rp1) \
		-A 192.0.2.1 -B 192.0.2.2 -t ip proto=103 -p 100 -q
}

ipv6_pim_test()
{
	devlink_trap_stats_test "IPv6 PIM - Multicast" "ipv6_pim" \
		$MZ $h1 -6 -c 1 -a own -b 33:33:00:00:00:0d \
		-A fe80::1 -B ff02::d -t ip next=103 -p 100 -q

	devlink_trap_stats_test "IPv6 PIM - Unicast" "ipv6_pim" \
		$MZ $h1 -6 -c 1 -a own -b $(mac_get $rp1) \
		-A fe80::1 -B 2001:db8:1::2 -t ip next=103 -p 100 -q
}

uc_loopback_test()
{
	# Add neighbours to the fake destination IPs, so that the packets are
	# routed in the device and not trapped due to an unresolved neighbour
	# exception.
	ip -4 neigh add 192.0.2.3 lladdr 00:11:22:33:44:55 nud permanent \
		dev $rp1
	ip -6 neigh add 2001:db8:1::3 lladdr 00:11:22:33:44:55 nud permanent \
		dev $rp1

	devlink_trap_stats_test "IPv4 Unicast Loopback" "uc_loopback" \
		$MZ $h1 -c 1 -a own -b $(mac_get $rp1) \
		-A 192.0.2.1 -B 192.0.2.3 -t udp sp=54321,dp=12345 -p 100 -q

	devlink_trap_stats_test "IPv6 Unicast Loopback" "uc_loopback" \
		$MZ $h1 -6 -c 1 -a own -b $(mac_get $rp1) \
		-A 2001:db8:1::1 -B 2001:db8:1::3 -t udp sp=54321,dp=12345 \
		-p 100 -q

	ip -6 neigh del 2001:db8:1::3 dev $rp1
	ip -4 neigh del 192.0.2.3 dev $rp1
}

local_route_test()
{
	# Use a fake source IP to prevent the trap from being triggered twice
	# when the router sends back a port unreachable message.
	devlink_trap_stats_test "IPv4 Local Route" "local_route" \
		$MZ $h1 -c 1 -a own -b $(mac_get $rp1) \
		-A 192.0.2.3 -B 192.0.2.2 -t udp sp=54321,dp=12345 -p 100 -q

	devlink_trap_stats_test "IPv6 Local Route" "local_route" \
		$MZ $h1 -6 -c 1 -a own -b $(mac_get $rp1) \
		-A 2001:db8:1::3 -B 2001:db8:1::2 -t udp sp=54321,sp=12345 \
		-p 100 -q
}

external_route_test()
{
	# Add a dummy device through which the incoming packets should be
	# routed.
	ip link add name dummy10 up type dummy
	ip address add 203.0.113.1/24 dev dummy10
	ip -6 address add 2001:db8:10::1/64 dev dummy10

	devlink_trap_stats_test "IPv4 External Route" "external_route" \
		$MZ $h1 -c 1 -a own -b $(mac_get $rp1) \
		-A 192.0.2.1 -B 203.0.113.2 -t udp sp=54321,dp=12345 -p 100 -q

	devlink_trap_stats_test "IPv6 External Route" "external_route" \
		$MZ $h1 -6 -c 1 -a own -b $(mac_get $rp1) \
		-A 2001:db8:1::1 -B 2001:db8:10::2 -t udp sp=54321,sp=12345 \
		-p 100 -q

	ip -6 address del 2001:db8:10::1/64 dev dummy10
	ip address del 203.0.113.1/24 dev dummy10
	ip link del dev dummy10
}

ipv6_uc_dip_link_local_scope_test()
{
	# Add a dummy link-local prefix route to allow the packet to be routed.
	ip -6 route add fe80:1::/64 dev $rp2

	devlink_trap_stats_test \
		"IPv6 Unicast Destination IP With Link-Local Scope" \
		"ipv6_uc_dip_link_local_scope" \
		$MZ $h1 -6 -c 1 -a own -b $(mac_get $rp1) \
		-A fe80::1 -B fe80:1::2 -t udp sp=54321,sp=12345 \
		-p 100 -q

	ip -6 route del fe80:1::/64 dev $rp2
}

ipv4_router_alert_get()
{
	local p

	# https://en.wikipedia.org/wiki/IPv4#Options
	p=$(:
		)"94:"$(			: Option Number
		)"04:"$(			: Option Length
		)"00:00:"$(			: Option Data
		)
	echo $p
}

ipv4_router_alert_test()
{
	devlink_trap_stats_test "IPv4 Router Alert" "ipv4_router_alert" \
		$MZ $h1 -c 1 -a own -b $(mac_get $rp1) \
		-A 192.0.2.1 -B 198.51.100.3 \
		-t ip option=$(ipv4_router_alert_get) -p 100 -q
}

ipv6_router_alert_get()
{
	local p

	# https://en.wikipedia.org/wiki/IPv6_packet#Hop-by-hop_options_and_destination_options
	# https://tools.ietf.org/html/rfc2711#section-2.1
	p=$(:
		)"11:"$(			: Next Header - UDP
		)"00:"$(			: Hdr Ext Len
		)"05:02:00:00:00:00:"$(		: Option Data
		)
	echo $p
}

ipv6_router_alert_test()
{
	devlink_trap_stats_test "IPv6 Router Alert" "ipv6_router_alert" \
		$MZ $h1 -6 -c 1 -a own -b $(mac_get $rp1) \
		-A 2001:db8:1::1 -B 2001:db8:1::3 \
		-t ip next=0,payload=$(ipv6_router_alert_get) -p 100 -q
}

ipv6_dip_all_nodes_test()
{
	devlink_trap_stats_test "IPv6 Destination IP \"All Nodes Address\"" \
		"ipv6_dip_all_nodes" \
		$MZ $h1 -6 -c 1 -a own -b 33:33:00:00:00:01 \
		-A 2001:db8:1::1 -B ff02::1 -t udp sp=12345,dp=54321 -p 100 -q
}

ipv6_dip_all_routers_test()
{
	devlink_trap_stats_test "IPv6 Destination IP \"All Routers Address\"" \
		"ipv6_dip_all_routers" \
		$MZ $h1 -6 -c 1 -a own -b 33:33:00:00:00:02 \
		-A 2001:db8:1::1 -B ff02::2 -t udp sp=12345,dp=54321 -p 100 -q
}

ipv6_router_solicit_test()
{
	devlink_trap_stats_test "IPv6 Router Solicitation" \
		"ipv6_router_solicit" \
		$MZ $h1 -6 -c 1 -a own -b 33:33:00:00:00:02 \
		-A fe80::1 -B ff02::2 \
		-t ip hop=1,next=58,payload=$(icmpv6_header_get 133) -p 100 -q
}

ipv6_router_advert_test()
{
	devlink_trap_stats_test "IPv6 Router Advertisement" \
		"ipv6_router_advert" \
		$MZ $h1 -6 -c 1 -a own -b 33:33:00:00:00:01 \
		-A fe80::1 -B ff02::1 \
		-t ip hop=1,next=58,payload=$(icmpv6_header_get 134) -p 100 -q
}

ipv6_redirect_test()
{
	devlink_trap_stats_test "IPv6 Redirect Message" \
		"ipv6_redirect" \
		$MZ $h1 -6 -c 1 -a own -b $(mac_get $rp1) \
		-A fe80::1 -B 2001:db8:1::2 \
		-t ip hop=1,next=58,payload=$(icmpv6_header_get 137) -p 100 -q
}

ptp_event_test()
{
	mlxsw_only_on_spectrum 1 || return

	# PTP Sync (0)
	devlink_trap_stats_test "PTP Time-Critical Event Message" "ptp_event" \
		$MZ $h1 -c 1 -a own -b 01:00:5e:00:01:81 \
		-A 192.0.2.1 -B 224.0.1.129 \
		-t udp sp=12345,dp=319,payload=10 -p 100 -q
}

ptp_general_test()
{
	mlxsw_only_on_spectrum 1 || return

	# PTP Announce (b)
	devlink_trap_stats_test "PTP General Message" "ptp_general" \
		$MZ $h1 -c 1 -a own -b 01:00:5e:00:01:81 \
		-A 192.0.2.1 -B 224.0.1.129 \
		-t udp sp=12345,dp=320,payload=1b -p 100 -q
}

flow_action_sample_test()
{
	# Install a filter that samples every incoming packet.
	tc qdisc add dev $rp1 clsact
	tc filter add dev $rp1 ingress proto all pref 1 handle 101 matchall \
		skip_sw action sample rate 1 group 1

	devlink_trap_stats_test "Flow Sampling" "flow_action_sample" \
		$MZ $h1 -c 1 -a own -b $(mac_get $rp1) \
		-A 192.0.2.1 -B 198.51.100.1 -t udp sp=12345,dp=54321 -p 100 -q

	tc filter del dev $rp1 ingress proto all pref 1 handle 101 matchall
	tc qdisc del dev $rp1 clsact
}

flow_action_trap_test()
{
	# Install a filter that traps a specific flow.
	tc qdisc add dev $rp1 clsact
	tc filter add dev $rp1 ingress proto ip pref 1 handle 101 flower \
		skip_sw ip_proto udp src_port 12345 dst_port 54321 action trap

	devlink_trap_stats_test "Flow Trapping (Logging)" "flow_action_trap" \
		$MZ $h1 -c 1 -a own -b $(mac_get $rp1) \
		-A 192.0.2.1 -B 198.51.100.1 -t udp sp=12345,dp=54321 -p 100 -q

	tc filter del dev $rp1 ingress proto ip pref 1 handle 101 flower
	tc qdisc del dev $rp1 clsact
}

eapol_payload_get()
{
	local source_mac=$1; shift
	local p

	p=$(:
		)"01:80:C2:00:00:03:"$(       : ETH daddr
		)"$source_mac:"$(             : ETH saddr
		)"88:8E:"$(                   : ETH type
		)
	echo $p
}

eapol_test()
{
	local h1mac=$(mac_get $h1)

	devlink_trap_stats_test "EAPOL" "eapol" $MZ $h1 -c 1 \
		$(eapol_payload_get $h1mac) -p 100 -q
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS
