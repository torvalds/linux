#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test that the state of the route exception cache after an ICMP error is
# processed does not depend on whether the quoted packet was matched to a
# socket. Otherwise, an off-path attacker can probe the cache to discover the
# ephemeral port used by a connected UDP socket.
#
# When the quoted packet is not matched to a socket, the same exception is
# created as when it is matched, so that neither its presence nor its contents
# reveal the result of socket matching.
#
#                              +----+
#                    +---------| r1 |
#                    |         +----+
#  +----+   +--------+            | .1
#  | h1 |---| bridge |            |    198.51.100.0/30
#  +----+   +--------+            |    2001:db8:2::/64
#      .1            |            | .2
#                    |         +----+           +----+
#                    +---------| r2 |-----------| h2 |
#                    .2   .3   +----+ .1     .2 +----+
#                                   203.0.113.0/24
#                                   2001:db8:3::/64
#         192.0.2.0/24
#         2001:db8:1::/64
#
# Traffic from h1 to h2 is routed via r1, which reaches h2's network via r2
# over the point-to-point link. The MTU of the r2 - h2 link is lowered so that
# r2 emits ICMP errors towards h1.
#
# For the redirect tests r1's route to h2's network is replaced with one via r2
# on the shared segment, so that r1 forwards the packet back to the segment it
# arrived from and emits a redirect towards h1.
#
# The packets that provoke the ICMP errors are injected with a packet socket so
# that no socket is ever associated with them. A socket is created separately,
# with socat, when a test needs the ICMP error to be matched.

# shellcheck disable=SC1091,SC2034,SC2154,SC2329
source lib.sh

require_command jq
require_command mausezahn
require_command nstat
require_command socat

ALL_TESTS="
	pmtu_no_socket_ipv4
	pmtu_no_socket_ipv6
	pmtu_socket_ipv4
	pmtu_socket_ipv6
	pmtu_omit_ipv4
	pmtu_omit_ipv6
	redirect_no_socket_ipv4
	redirect_no_socket_ipv6
	redirect_socket_ipv4
	redirect_socket_ipv6
"

# Shared segment.
H1_ADDR4=192.0.2.1
R1_ADDR4=192.0.2.2
R2_ADDR4=192.0.2.3
H1_ADDR6=2001:db8:1::1
R1_ADDR6=2001:db8:1::2
R2_ADDR6=2001:db8:1::3

# r1 - r2 link.
R2_R1_ADDR4=198.51.100.2
R2_R1_ADDR6=2001:db8:2::2

# r2 - h2 link.
H2_ADDR4=203.0.113.2
H2_NET4=203.0.113.0/24
H2_ADDR6=2001:db8:3::2
H2_NET6=2001:db8:3::/64

SPORT=12345
DPORT=54321

# The MTU of the shared segment and of the r1 - r2 link. Large enough for the
# injected packets to reach r2 intact.
SEGMENT_MTU=2000
# Size of the injected packets. The PMTU tests need a size that exceeds every
# MTU used for the r2 - h2 link, so that r2 responds with an ICMP error. The
# redirect tests need a size that does not, otherwise r2 would respond with an
# ICMP error in addition to the redirect emitted by r1.
PMTU_PACKET_SIZE=1800
REDIRECT_PACKET_SIZE=100

# The MTUs used for the r2 - h2 link. All of them must be at least
# IPV6_MIN_MTU, otherwise IPv6 silently ignores the error instead of creating
# an exception.
MTU_MID=1400
MTU_LOW=1300

# Values for the IP{,V6}_MTU_DISCOVER socket option.
PMTUDISC_DONT=0
PMTUDISC_OMIT=5

SOCAT_PID=

linklocal_get()
{
	local ns=$1; shift
	local dev=$1; shift

	ip -n "$ns" -j -6 addr show dev "$dev" | \
		jq -r '.[]["addr_info"][] | select(.scope == "link") | .local'
}

linklocal_exists()
{
	local ns=$1; shift
	local dev=$1; shift

	[ -n "$(linklocal_get "$ns" "$dev")" ]
}

family_vars_set()
{
	local family=$1; shift

	FAMILY=$family

	if [ "$family" -eq 4 ]; then
		H1_ADDR=$H1_ADDR4
		H2_ADDR=$H2_ADDR4
		MZ_FAMILY_OPT=()
		# Without the Don't Fragment bit set r2 fragments the packet
		# instead of reporting the MTU of the next hop.
		MZ_IP_OPTS="df,"
		SOCAT_DST="UDP4-CONNECT:$H2_ADDR4:$DPORT"
		SOCAT_BIND="bind=$H1_ADDR4:$SPORT"
		SOCAT_PMTUDISC="ip-mtu-discover"
	else
		H1_ADDR=$H1_ADDR6
		H2_ADDR=$H2_ADDR6
		MZ_FAMILY_OPT=(-6)
		MZ_IP_OPTS=
		SOCAT_DST="UDP6-CONNECT:[$H2_ADDR6]:$DPORT"
		SOCAT_BIND="bind=[$H1_ADDR6]:$SPORT"
		SOCAT_PMTUDISC="ipv6-mtu-discover"
	fi
}

topology_setup()
{
	local ns

	setup_ns h1 r1 r2 h2 sw
	defer cleanup_all_ns

	# Link-local addresses are generated from the MAC address and read
	# back during setup, so request that generation mode explicitly and
	# make the addresses available as soon as the devices are brought up.
	for ns in "$h1" "$r1" "$r2" "$h2" "$sw"; do
		ip netns exec "$ns" sysctl -qw \
			net.ipv6.conf.default.addr_gen_mode=0 \
			net.ipv6.conf.default.accept_dad=0 \
			net.ipv6.conf.all.accept_dad=0
	done

	ip -n "$sw" link add name br0 type bridge
	ip -n "$sw" link set dev br0 mtu "$SEGMENT_MTU" up

	ip -n "$h1" link add name eth0 mtu "$SEGMENT_MTU" type veth \
		peer name swp1 mtu "$SEGMENT_MTU" netns "$sw"
	ip -n "$r1" link add name eth0 mtu "$SEGMENT_MTU" type veth \
		peer name swp2 mtu "$SEGMENT_MTU" netns "$sw"
	ip -n "$r2" link add name eth0 mtu "$SEGMENT_MTU" type veth \
		peer name swp3 mtu "$SEGMENT_MTU" netns "$sw"
	ip -n "$r1" link add name eth1 mtu "$SEGMENT_MTU" type veth \
		peer name eth1 mtu "$SEGMENT_MTU" netns "$r2"
	ip -n "$r2" link add name eth2 type veth peer name eth0 netns "$h2"

	ip -n "$sw" link set dev swp1 master br0 up
	ip -n "$sw" link set dev swp2 master br0 up
	ip -n "$sw" link set dev swp3 master br0 up

	ip -n "$h1" link set dev eth0 up
	ip -n "$r1" link set dev eth0 up
	ip -n "$r1" link set dev eth1 up
	ip -n "$r2" link set dev eth0 up
	ip -n "$r2" link set dev eth1 up
	ip -n "$r2" link set dev eth2 up
	ip -n "$h2" link set dev eth0 up

	ip -n "$h1" address add "$H1_ADDR4/24" dev eth0
	ip -n "$r1" address add "$R1_ADDR4/24" dev eth0
	ip -n "$r2" address add "$R2_ADDR4/24" dev eth0
	ip -n "$r1" address add 198.51.100.1/30 dev eth1
	ip -n "$r2" address add "$R2_R1_ADDR4/30" dev eth1
	ip -n "$r2" address add 203.0.113.1/24 dev eth2
	ip -n "$h2" address add "$H2_ADDR4/24" dev eth0

	ip -n "$h1" -6 address add "$H1_ADDR6/64" dev eth0 nodad
	ip -n "$r1" -6 address add "$R1_ADDR6/64" dev eth0 nodad
	ip -n "$r2" -6 address add "$R2_ADDR6/64" dev eth0 nodad
	ip -n "$r1" -6 address add 2001:db8:2::1/64 dev eth1 nodad
	ip -n "$r2" -6 address add "$R2_R1_ADDR6/64" dev eth1 nodad
	ip -n "$r2" -6 address add 2001:db8:3::1/64 dev eth2 nodad
	ip -n "$h2" -6 address add "$H2_ADDR6/64" dev eth0 nodad

	ip netns exec "$r1" sysctl -qw net.ipv4.ip_forward=1
	ip netns exec "$r1" sysctl -qw net.ipv4.conf.all.send_redirects=1
	ip netns exec "$r1" sysctl -qw net.ipv6.conf.all.forwarding=1
	ip netns exec "$r2" sysctl -qw net.ipv4.ip_forward=1
	ip netns exec "$r2" sysctl -qw net.ipv6.conf.all.forwarding=1

	ip netns exec "$h1" sysctl -qw net.ipv4.conf.all.accept_redirects=1
	ip netns exec "$h1" sysctl -qw net.ipv4.conf.eth0.accept_redirects=1
	ip netns exec "$h1" sysctl -qw net.ipv6.conf.all.accept_redirects=1
	ip netns exec "$h1" sysctl -qw net.ipv6.conf.eth0.accept_redirects=1

	slowwait 5 linklocal_exists "$r1" eth0
	check_err $? "r1: link-local address was not generated"
	slowwait 5 linklocal_exists "$r2" eth0
	check_err $? "r2: link-local address was not generated"

	R1_LLADDR=$(linklocal_get "$r1" eth0)
	R2_LLADDR=$(linklocal_get "$r2" eth0)
	R1_MAC=$(ip -n "$r1" -j link show dev eth0 | jq -r '.[]["address"]')
	R2_MAC=$(ip -n "$r2" -j link show dev eth0 | jq -r '.[]["address"]')

	ip -n "$h1" route add "$H2_NET4" via "$R1_ADDR4" dev eth0
	ip -n "$h1" -6 route add "$H2_NET6" via "$R1_LLADDR" dev eth0
	ip -n "$r1" route add "$H2_NET4" via "$R2_R1_ADDR4" dev eth1
	ip -n "$r1" -6 route add "$H2_NET6" via "$R2_R1_ADDR6" dev eth1
	ip -n "$h2" route add default via 203.0.113.1 dev eth0
	ip -n "$h2" -6 route add default via 2001:db8:3::1 dev eth0

	far_mtu_set "$MTU_MID"
}

# Make r1 forward towards h2's network over the segment it receives the packet
# from, so that it emits a redirect towards h1.
redirect_route_set()
{
	ip -n "$r1" route replace "$H2_NET4" via "$R2_ADDR4" dev eth0
	ip -n "$r1" -6 route replace "$H2_NET6" via "$R2_LLADDR" dev eth0

	# __ip_do_redirect() only creates an exception if the new gateway is
	# already a valid neighbour. Otherwise it merely triggers address
	# resolution. IPv6 resolves the target itself, in rt6_do_redirect().
	ip -n "$h1" neigh replace "$R2_ADDR4" lladdr "$R2_MAC" dev eth0 \
		nud permanent
}

far_mtu_set()
{
	local mtu=$1; shift

	ip -n "$r2" link set dev eth2 mtu "$mtu"
	ip -n "$h2" link set dev eth0 mtu "$mtu"
}

socket_is_open()
{
	ip netns exec "$h1" ss -uHn "sport = :$SPORT" | grep -q .
}

socket_start()
{
	# Disable PMTU discovery by default so that ICMP errors are not
	# reported to the socket. Otherwise socat would exit when the first one
	# arrives and later packets in the same test would not be matched to a
	# socket. The exception is still created, as ip{,6}_sk_accept_pmtu()
	# only rejects IP{,V6}_PMTUDISC_{INTERFACE,OMIT}.
	local pmtudisc=${1:-$PMTUDISC_DONT}

	# Send socat's diagnostics to /dev/null. It reports the ICMP errors
	# that reach the socket, which is exactly what the tests provoke.
	ip netns exec "$h1" socat -u -lf/dev/null \
		"$SOCAT_DST,$SOCAT_BIND,$SOCAT_PMTUDISC=$pmtudisc" \
		OPEN:/dev/null,wronly=1 &
	SOCAT_PID=$!
	defer socket_stop

	slowwait 5 socket_is_open
	check_err $? "socket did not open"
}

socket_stop()
{
	[ -z "$SOCAT_PID" ] && return 0

	kill "$SOCAT_PID" &> /dev/null
	wait "$SOCAT_PID" 2> /dev/null
	SOCAT_PID=
}

# Inject a packet towards h2 with a packet socket. No socket is associated with
# it, so an ICMP error quoting it is matched to a socket only if one was
# created separately with the same source port.
packet_send()
{
	local size=$1; shift

	ip netns exec "$h1" mausezahn "${MZ_FAMILY_OPT[@]}" eth0 \
		-a own -b "$R1_MAC" -A "$H1_ADDR" -B "$H2_ADDR" \
		-t udp "${MZ_IP_OPTS}sp=$SPORT,dp=$DPORT" \
		-p "$size" -c 1 -q
}

exception_show()
{
	if [ "$FAMILY" -eq 4 ]; then
		# IPv4 exceptions without a bound route are not dumped, but
		# "route get" reports the exception and binds a route to it.
		ip -n "$h1" route get "$H2_ADDR"
	else
		# IPv6 does not report a cache indication in "route get"
		# output, so dump the exceptions instead.
		ip -n "$h1" -6 route show cache | grep -F "$H2_ADDR" || true
	fi
}

exception_mtu_get()
{
	exception_show | grep -o "mtu [0-9]*" | cut -d ' ' -f 2
}

exception_gw_get()
{
	exception_show | grep -o "via [0-9a-f.:]*" | cut -d ' ' -f 2
}

exception_mtu_check()
{
	local expected=$1; shift

	[ "$(exception_mtu_get)" = "$expected" ]
}

icmp_errors_get()
{
	local ctr=IcmpInDestUnreachs

	[ "$FAMILY" -eq 6 ] && ctr=Icmp6InPktTooBigs

	ip netns exec "$h1" nstat -asz "$ctr" | \
		awk -v ctr="$ctr" '$1 == ctr { print $2 }'
}

exception_pmtu_check()
{
	local mtu=$1; shift
	local desc=$1; shift

	busywait "$BUSYWAIT_TIMEOUT" exception_mtu_check "$mtu"
	check_err $? "$desc: exception does not carry an MTU of $mtu"
}

pmtu_no_socket()
{
	local family=$1; shift

	RET=0
	family_vars_set "$family"
	topology_setup

	packet_send "$PMTU_PACKET_SIZE"
	exception_pmtu_check "$MTU_MID" "No socket"

	log_test "IPv$family: PMTU: exception without a matching socket"
}

pmtu_no_socket_ipv4()
{
	pmtu_no_socket 4
}

pmtu_no_socket_ipv6()
{
	pmtu_no_socket 6
}

pmtu_socket()
{
	local family=$1; shift
	local t0

	RET=0
	family_vars_set "$family"
	topology_setup
	socket_start

	packet_send "$PMTU_PACKET_SIZE"
	exception_pmtu_check "$MTU_MID" "Matching socket"

	# A lower PMTU replaces the one currently stored in the exception.
	far_mtu_set "$MTU_LOW"
	packet_send "$PMTU_PACKET_SIZE"
	exception_pmtu_check "$MTU_LOW" "Lower PMTU"

	# A higher PMTU is ignored, so the exception is left as it is. Wait
	# for the error to be received, as otherwise the check below would
	# pass even if it never was.
	far_mtu_set "$MTU_MID"
	t0=$(icmp_errors_get)
	packet_send "$PMTU_PACKET_SIZE"
	busywait "$BUSYWAIT_TIMEOUT" until_counter_is ">= $((t0 + 1))" \
		icmp_errors_get > /dev/null
	check_err $? "Higher PMTU: ICMP error was not received"

	exception_mtu_check "$MTU_LOW"
	check_err $? "Higher PMTU: exception does not carry an MTU of $MTU_LOW"

	log_test "IPv$family: PMTU: exception with a matching socket"
}

pmtu_socket_ipv4()
{
	pmtu_socket 4
}

pmtu_socket_ipv6()
{
	pmtu_socket 6
}

pmtu_omit()
{
	local family=$1; shift

	RET=0
	family_vars_set "$family"
	topology_setup
	socket_start "$PMTUDISC_OMIT"

	packet_send "$PMTU_PACKET_SIZE"
	exception_pmtu_check "$MTU_MID" "PMTU discovery disabled"

	log_test "IPv$family: PMTU: exception with a socket ignoring it"
}

pmtu_omit_ipv4()
{
	pmtu_omit 4
}

pmtu_omit_ipv6()
{
	pmtu_omit 6
}

exception_gw_check()
{
	local expected=$1; shift

	[ -n "$expected" ] && [ "$(exception_gw_get)" = "$expected" ]
}

redirect_gw_new()
{
	if [ "$FAMILY" -eq 4 ]; then
		echo "$R2_ADDR4"
	else
		echo "$R2_LLADDR"
	fi
}

redirect_no_socket()
{
	local family=$1; shift

	RET=0
	family_vars_set "$family"
	topology_setup
	redirect_route_set

	packet_send "$REDIRECT_PACKET_SIZE"
	busywait "$BUSYWAIT_TIMEOUT" exception_gw_check "$(redirect_gw_new)"
	check_err $? "No socket: exception does not carry the new gateway"

	log_test "IPv$family: Redirect: exception without a matching socket"
}

redirect_no_socket_ipv4()
{
	redirect_no_socket 4
}

redirect_no_socket_ipv6()
{
	redirect_no_socket 6
}

redirect_socket()
{
	local family=$1; shift

	RET=0
	family_vars_set "$family"
	topology_setup
	redirect_route_set
	socket_start

	packet_send "$REDIRECT_PACKET_SIZE"
	busywait "$BUSYWAIT_TIMEOUT" exception_gw_check "$(redirect_gw_new)"
	check_err $? "Matching socket: exception does not carry the new gateway"

	log_test "IPv$family: Redirect: exception with a matching socket"
}

redirect_socket_ipv4()
{
	redirect_socket 4
}

redirect_socket_ipv6()
{
	redirect_socket 6
}

trap defer_scopes_cleanup EXIT
tests_run

exit "$EXIT_STATUS"
