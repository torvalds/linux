#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Copyright 2026 Google LLC.
#
# This test verifies TCP flow failover between ECMP routes
# upon carrier loss on the active device.
#
#   socat  ----------------------------->  socat
#                        |
#           .-- veth-c1 -|- veth-s1 --.
#   dummy0 -|            |            |-- dummy0
#           '-- veth-c2 -|- veth-s2 --'
#                        |
#

REQUIRE_JQ=no
REQUIRE_MZ=no
NUM_NETIFS=0

source forwarding/lib.sh

CLIENT_IP="10.0.59.1"
SERVER_IP="10.0.92.1"
CLIENT_IP6="2001:db8:5a9a::1"
SERVER_IP6="2001:db8:9292::1"

setup_server()
{
	IP="ip -n $server"
	NS_EXEC="ip netns exec $server"

	$IP link add dummy0 type dummy
	$IP link set dummy0 up

	$IP -4 addr add $SERVER_IP/32 dev dummy0
	$IP -6 addr add $SERVER_IP6/128 dev dummy0 nodad

	$IP link set veth-s1 up
	$IP link set veth-s2 up

	$IP -4 addr add 192.168.1.2/24 dev veth-s1
	$IP -4 addr add 192.168.2.2/24 dev veth-s2

	$IP -4 route add $CLIENT_IP/32 \
		nexthop via 192.168.1.1 dev veth-s1 weight 1 \
		nexthop via 192.168.2.1 dev veth-s2 weight 1

	$IP -6 addr add 2001:db8:1::2/64 dev veth-s1 nodad
	$IP -6 addr add 2001:db8:2::2/64 dev veth-s2 nodad

	$IP -6 route add $CLIENT_IP6/128 \
		nexthop via 2001:db8:1::1 dev veth-s1 weight 1 \
		nexthop via 2001:db8:2::1 dev veth-s2 weight 1
}

setup_client()
{
	IP="ip -n $client"
	NS_EXEC="ip netns exec $client"

	$IP link add dummy0 type dummy
	$IP link set dummy0 up

	$IP -4 addr add $CLIENT_IP/32 dev dummy0
	$IP -6 addr add $CLIENT_IP6/128 dev dummy0 nodad

	$IP link set veth-c1 up
	$IP link set veth-c2 up

	$IP -4 addr add 192.168.1.1/24 dev veth-c1
	$IP -4 addr add 192.168.2.1/24 dev veth-c2

	$IP -4 route add $SERVER_IP/32 \
		nexthop via 192.168.1.2 dev veth-c1 weight 1 \
		nexthop via 192.168.2.2 dev veth-c2 weight 1

	$IP -6 addr add 2001:db8:1::1/64 dev veth-c1 nodad
	$IP -6 addr add 2001:db8:2::1/64 dev veth-c2 nodad

	$IP -6 route add $SERVER_IP6/128 \
		nexthop via 2001:db8:1::2 dev veth-c1 weight 1 \
		nexthop via 2001:db8:2::2 dev veth-c2 weight 1

	# By default, tcp_retries1=3 triggers a route refresh
	# after 3 retransmits (~5s).  Ensure this never occurs
	# for test stability.
	$NS_EXEC sysctl -qw net.ipv4.tcp_retries1=100

	# When NETDEV_CHANGE is issued for a dev tied to an ECMP
	# route, RTNH_F_LINKDOWN is flagged and the sernum is
	# bumped to invalidate the route via sk_dst_check().
	#
	# Without ignore_routes_with_linkdown=1, subsequent
	# lookups may still select the same RTNH_F_LINKDOWN route.
	$NS_EXEC sysctl -qw net.ipv4.conf.veth-c1.ignore_routes_with_linkdown=1
	$NS_EXEC sysctl -qw net.ipv4.conf.veth-c2.ignore_routes_with_linkdown=1

	$NS_EXEC sysctl -qw net.ipv6.conf.veth-c1.ignore_routes_with_linkdown=1
	$NS_EXEC sysctl -qw net.ipv6.conf.veth-c2.ignore_routes_with_linkdown=1
}

setup()
{
	setup_ns client server

	ip -n "$client" link add veth-c1 type veth peer veth-s1 netns "$server"
	ip -n "$client" link add veth-c2 type veth peer veth-s2 netns "$server"

	setup_server
	setup_client
}

cleanup()
{
	cleanup_all_ns > /dev/null 2>&1
}

tcp_ecmp_failover()
{
	local pf=$1; shift
	local server_ip=$1; shift
	local client_ip=$1; shift

	RET=0

	tcpdump_start veth-s1 "$server"
	tcpdump_start veth-s2 "$server"

	ip netns exec "$server" \
		socat -u TCP-LISTEN:8080,pf="$pf",bind="$server_ip",reuseaddr /dev/null &
	server_pid=$!

	# Wait for server to start listening.
	# Sometimes client fails without this sleep.
	sleep 1

	ip netns exec "$client" \
		socat -u /dev/zero TCP:"$server_ip":8080,pf="$pf",bind="$client_ip" &
	client_pid=$!

	# To capture enough packets.
	sleep 3

	tcpdump_stop veth-s1
	tcpdump_stop veth-s2

	pkts_s1=$(tcpdump_show veth-s1 | wc -l)
	pkts_s2=$(tcpdump_show veth-s2 | wc -l)

	tcpdump_cleanup veth-s1
	tcpdump_cleanup veth-s2

	# Detect the device chosen by the client
	if [ "$pkts_s1" -gt "$pkts_s2" ]; then
		veth_down=veth-s1
		veth_up=veth-s2
	else
		veth_down=veth-s2
		veth_up=veth-s1
	fi

	# Taking down $veth_down causes its peer to lose carrier,
	# triggering NETDEV_CHANGE.  This flags RTNH_F_LINKDOWN
	# and bumps the sernum for the route associated with that
	# peer, invalidating the cached dst in the TCP socket.
	#
	# Consequently, sk_dst_check() fails, forcing the subsequent
	# lookup to select the remaining healthy route via $veth_up.
	ip -n "$server" link set "$veth_down" down

	tcpdump_start "$veth_up" "$server"

	# To capture enough packets.
	sleep  3

	tcpdump_stop "$veth_up"

	kill -9 "$client_pid" > /dev/null 2>&1
	kill -9 "$server_pid" > /dev/null 2>&1
	wait 2> /dev/null

	pkts=$(tcpdump_show $veth_up | wc -l)

	tcpdump_cleanup "$veth_up"

	if [ "$pkts" -lt 1000 ]; then
		RET=$ksft_fail
	fi
}

test_ipv4()
{
	setup
	tcp_ecmp_failover IPv4 $SERVER_IP $CLIENT_IP
	log_test "TCP IPv4 failover"
	cleanup
}

test_ipv6()
{
	setup
	tcp_ecmp_failover IPv6 "[$SERVER_IP6]" "[$CLIENT_IP6]"
	log_test "TCP IPv6 failover"
	cleanup
}

require_command socat
require_command tcpdump

trap cleanup EXIT

test_ipv4
test_ipv6

exit "$EXIT_STATUS"
