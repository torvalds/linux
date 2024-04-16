#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

# Test various bareudp tunnel configurations.
#
# The bareudp module allows to tunnel network protocols like IP or MPLS over
# UDP, without adding any intermediate header. This scripts tests several
# configurations of bareudp (using IPv4 or IPv6 as underlay and transporting
# IPv4, IPv6 or MPLS packets on the overlay).
#
# Network topology:
#
#   * A chain of 4 network namespaces, connected with veth pairs. Each veth
#     is assigned an IPv4 and an IPv6 address. A host-route allows a veth to
#     join its peer.
#
#   * NS0 and NS3 are at the extremities of the chain. They have additional
#     IPv4 and IPv6 addresses on their loopback device. Routes are added in NS0
#     and NS3, so that they can communicate using these overlay IP addresses.
#     For IPv4 and IPv6 reachability tests, the route simply sets the peer's
#     veth address as gateway. For MPLS reachability tests, an MPLS header is
#     also pushed before the IP header.
#
#   * NS1 and NS2 are the intermediate namespaces. They use a bareudp device to
#     encapsulate the traffic into UDP.
#
# +-----------------------------------------------------------------------+
# |                                  NS0                                  |
# |                                                                       |
# |   lo:                                                                 |
# |      * IPv4 address: 192.0.2.100/32                                   |
# |      * IPv6 address: 2001:db8::100/128                                |
# |      * IPv6 address: 2001:db8::200/128                                |
# |      * IPv4 route: 192.0.2.103/32 reachable via 192.0.2.11            |
# |      * IPv6 route: 2001:db8::103/128 reachable via 2001:db8::11       |
# |      * IPv6 route: 2001:db8::203/128 reachable via 2001:db8::11       |
# |                    (encapsulated with MPLS label 203)                 |
# |                                                                       |
# |   veth01:                                                             |
# |   ^  * IPv4 address: 192.0.2.10, peer 192.0.2.11/32                   |
# |   |  * IPv6 address: 2001:db8::10, peer 2001:db8::11/128              |
# |   |                                                                   |
# +---+-------------------------------------------------------------------+
#     |
#     | Traffic type: IP or MPLS (depending on test)
#     |
# +---+-------------------------------------------------------------------+
# |   |                              NS1                                  |
# |   |                                                                   |
# |   v                                                                   |
# |   veth10:                                                             |
# |      * IPv4 address: 192.0.2.11, peer 192.0.2.10/32                   |
# |      * IPv6 address: 2001:db8::11, peer 2001:db8::10/128              |
# |                                                                       |
# |   bareudp_ns1:                                                        |
# |      * Encapsulate IP or MPLS packets received on veth10 into UDP     |
# |        and send the resulting packets through veth12.                 |
# |      * Decapsulate bareudp packets (either IP or MPLS, over UDP)      |
# |        received on veth12 and send the inner packets through veth10.  |
# |                                                                       |
# |   veth12:                                                             |
# |   ^  * IPv4 address: 192.0.2.21, peer 192.0.2.22/32                   |
# |   |  * IPv6 address: 2001:db8::21, peer 2001:db8::22/128              |
# |   |                                                                   |
# +---+-------------------------------------------------------------------+
#     |
#     | Traffic type: IP or MPLS (depending on test), over UDP
#     |
# +---+-------------------------------------------------------------------+
# |   |                              NS2                                  |
# |   |                                                                   |
# |   v                                                                   |
# |   veth21:                                                             |
# |      * IPv4 address: 192.0.2.22, peer 192.0.2.21/32                   |
# |      * IPv6 address: 2001:db8::22, peer 2001:db8::21/128              |
# |                                                                       |
# |   bareudp_ns2:                                                        |
# |      * Decapsulate bareudp packets (either IP or MPLS, over UDP)      |
# |        received on veth21 and send the inner packets through veth23.  |
# |      * Encapsulate IP or MPLS packets received on veth23 into UDP     |
# |        and send the resulting packets through veth21.                 |
# |                                                                       |
# |   veth23:                                                             |
# |   ^  * IPv4 address: 192.0.2.32, peer 192.0.2.33/32                   |
# |   |  * IPv6 address: 2001:db8::32, peer 2001:db8::33/128              |
# |   |                                                                   |
# +---+-------------------------------------------------------------------+
#     |
#     | Traffic type: IP or MPLS (depending on test)
#     |
# +---+-------------------------------------------------------------------+
# |   |                              NS3                                  |
# |   v                                                                   |
# |   veth32:                                                             |
# |      * IPv4 address: 192.0.2.33, peer 192.0.2.32/32                   |
# |      * IPv6 address: 2001:db8::33, peer 2001:db8::32/128              |
# |                                                                       |
# |   lo:                                                                 |
# |      * IPv4 address: 192.0.2.103/32                                   |
# |      * IPv6 address: 2001:db8::103/128                                |
# |      * IPv6 address: 2001:db8::203/128                                |
# |      * IPv4 route: 192.0.2.100/32 reachable via 192.0.2.32            |
# |      * IPv6 route: 2001:db8::100/128 reachable via 2001:db8::32       |
# |      * IPv6 route: 2001:db8::200/128 reachable via 2001:db8::32       |
# |                    (encapsulated with MPLS label 200)                 |
# |                                                                       |
# +-----------------------------------------------------------------------+

ERR=4 # Return 4 by default, which is the SKIP code for kselftest
PING6="ping"
PAUSE_ON_FAIL="no"

readonly NS0=$(mktemp -u ns0-XXXXXXXX)
readonly NS1=$(mktemp -u ns1-XXXXXXXX)
readonly NS2=$(mktemp -u ns2-XXXXXXXX)
readonly NS3=$(mktemp -u ns3-XXXXXXXX)

# Exit the script after having removed the network namespaces it created
#
# Parameters:
#
#   * The list of network namespaces to delete before exiting.
#
exit_cleanup()
{
	for ns in "$@"; do
		ip netns delete "${ns}" 2>/dev/null || true
	done

	if [ "${ERR}" -eq 4 ]; then
		echo "Error: Setting up the testing environment failed." >&2
	fi

	exit "${ERR}"
}

# Create the four network namespaces used by the script (NS0, NS1, NS2 and NS3)
#
# New namespaces are cleaned up manually in case of error, to ensure that only
# namespaces created by this script are deleted.
create_namespaces()
{
	ip netns add "${NS0}" || exit_cleanup
	ip netns add "${NS1}" || exit_cleanup "${NS0}"
	ip netns add "${NS2}" || exit_cleanup "${NS0}" "${NS1}"
	ip netns add "${NS3}" || exit_cleanup "${NS0}" "${NS1}" "${NS2}"
}

# The trap function handler
#
exit_cleanup_all()
{
	exit_cleanup "${NS0}" "${NS1}" "${NS2}" "${NS3}"
}

# Configure a network interface using a host route
#
# Parameters
#
#   * $1: the netns the network interface resides in,
#   * $2: the network interface name,
#   * $3: the local IPv4 address to assign to this interface,
#   * $4: the IPv4 address of the remote network interface,
#   * $5: the local IPv6 address to assign to this interface,
#   * $6: the IPv6 address of the remote network interface.
#
iface_config()
{
	local NS="${1}"; readonly NS
	local DEV="${2}"; readonly DEV
	local LOCAL_IP4="${3}"; readonly LOCAL_IP4
	local PEER_IP4="${4}"; readonly PEER_IP4
	local LOCAL_IP6="${5}"; readonly LOCAL_IP6
	local PEER_IP6="${6}"; readonly PEER_IP6

	ip -netns "${NS}" link set dev "${DEV}" up
	ip -netns "${NS}" address add dev "${DEV}" "${LOCAL_IP4}" peer "${PEER_IP4}"
	ip -netns "${NS}" address add dev "${DEV}" "${LOCAL_IP6}" peer "${PEER_IP6}" nodad
}

# Create base networking topology:
#
#   * set up the loopback device in all network namespaces (NS0..NS3),
#   * set up a veth pair to connect each netns in sequence (NS0 with NS1,
#     NS1 with NS2, etc.),
#   * add and IPv4 and an IPv6 address on each veth interface,
#   * prepare the ingress qdiscs in the intermediate namespaces.
#
setup_underlay()
{
	for ns in "${NS0}" "${NS1}" "${NS2}" "${NS3}"; do
		ip -netns "${ns}" link set dev lo up
	done;

	ip link add name veth01 netns "${NS0}" type veth peer name veth10 netns "${NS1}"
	ip link add name veth12 netns "${NS1}" type veth peer name veth21 netns "${NS2}"
	ip link add name veth23 netns "${NS2}" type veth peer name veth32 netns "${NS3}"
	iface_config "${NS0}" veth01 192.0.2.10 192.0.2.11/32 2001:db8::10 2001:db8::11/128
	iface_config "${NS1}" veth10 192.0.2.11 192.0.2.10/32 2001:db8::11 2001:db8::10/128
	iface_config "${NS1}" veth12 192.0.2.21 192.0.2.22/32 2001:db8::21 2001:db8::22/128
	iface_config "${NS2}" veth21 192.0.2.22 192.0.2.21/32 2001:db8::22 2001:db8::21/128
	iface_config "${NS2}" veth23 192.0.2.32 192.0.2.33/32 2001:db8::32 2001:db8::33/128
	iface_config "${NS3}" veth32 192.0.2.33 192.0.2.32/32 2001:db8::33 2001:db8::32/128

	tc -netns "${NS1}" qdisc add dev veth10 ingress
	tc -netns "${NS2}" qdisc add dev veth23 ingress
}

# Set up the IPv4, IPv6 and MPLS overlays.
#
# Configuration is similar for all protocols:
#
#   * add an overlay IP address on the loopback interface of each edge
#     namespace,
#   * route these IP addresses via the intermediate namespaces (for the MPLS
#     tests, this is also where MPLS encapsulation is done),
#   * add routes for these IP addresses (or MPLS labels) in the intermediate
#     namespaces.
#
# The bareudp encapsulation isn't configured in setup_overlay_*(). That will be
# done just before running the reachability tests.

setup_overlay_ipv4()
{
	# Add the overlay IP addresses and route them through the veth devices
	ip -netns "${NS0}" address add 192.0.2.100/32 dev lo
	ip -netns "${NS3}" address add 192.0.2.103/32 dev lo
	ip -netns "${NS0}" route add 192.0.2.103/32 src 192.0.2.100 via 192.0.2.11
	ip -netns "${NS3}" route add 192.0.2.100/32 src 192.0.2.103 via 192.0.2.32

	# Route the overlay addresses in the intermediate namespaces
	# (used after bareudp decapsulation)
	ip netns exec "${NS1}" sysctl -qw net.ipv4.ip_forward=1
	ip netns exec "${NS2}" sysctl -qw net.ipv4.ip_forward=1
	ip -netns "${NS1}" route add 192.0.2.100/32 via 192.0.2.10
	ip -netns "${NS2}" route add 192.0.2.103/32 via 192.0.2.33

	# The intermediate namespaces don't have routes for the reverse path,
	# as it will be handled by tc. So we need to ensure that rp_filter is
	# not going to block the traffic.
	ip netns exec "${NS1}" sysctl -qw net.ipv4.conf.all.rp_filter=0
	ip netns exec "${NS2}" sysctl -qw net.ipv4.conf.all.rp_filter=0
	ip netns exec "${NS1}" sysctl -qw net.ipv4.conf.default.rp_filter=0
	ip netns exec "${NS2}" sysctl -qw net.ipv4.conf.default.rp_filter=0
}

setup_overlay_ipv6()
{
	# Add the overlay IP addresses and route them through the veth devices
	ip -netns "${NS0}" address add 2001:db8::100/128 dev lo
	ip -netns "${NS3}" address add 2001:db8::103/128 dev lo
	ip -netns "${NS0}" route add 2001:db8::103/128 src 2001:db8::100 via 2001:db8::11
	ip -netns "${NS3}" route add 2001:db8::100/128 src 2001:db8::103 via 2001:db8::32

	# Route the overlay addresses in the intermediate namespaces
	# (used after bareudp decapsulation)
	ip netns exec "${NS1}" sysctl -qw net.ipv6.conf.all.forwarding=1
	ip netns exec "${NS2}" sysctl -qw net.ipv6.conf.all.forwarding=1
	ip -netns "${NS1}" route add 2001:db8::100/128 via 2001:db8::10
	ip -netns "${NS2}" route add 2001:db8::103/128 via 2001:db8::33
}

setup_overlay_mpls()
{
	# Add specific overlay IP addresses, routed over MPLS
	ip -netns "${NS0}" address add 2001:db8::200/128 dev lo
	ip -netns "${NS3}" address add 2001:db8::203/128 dev lo
	ip -netns "${NS0}" route add 2001:db8::203/128 src 2001:db8::200 encap mpls 203 via 2001:db8::11
	ip -netns "${NS3}" route add 2001:db8::200/128 src 2001:db8::203 encap mpls 200 via 2001:db8::32

	# Route the MPLS packets in the intermediate namespaces
	# (used after bareudp decapsulation)
	ip netns exec "${NS1}" sysctl -qw net.mpls.platform_labels=256
	ip netns exec "${NS2}" sysctl -qw net.mpls.platform_labels=256
	ip -netns "${NS1}" -family mpls route add 200 via inet6 2001:db8::10
	ip -netns "${NS2}" -family mpls route add 203 via inet6 2001:db8::33
}

# Run "ping" from NS0 and print the result
#
# Parameters:
#
#   * $1: the variant of ping to use (normally either "ping" or "ping6"),
#   * $2: the IP address to ping,
#   * $3: a human readable description of the purpose of the test.
#
# If the test fails and PAUSE_ON_FAIL is active, the user is given the
# possibility to continue with the next test or to quit immediately.
#
ping_test_one()
{
	local PING="$1"; readonly PING
	local IP="$2"; readonly IP
	local MSG="$3"; readonly MSG
	local RET

	printf "TEST: %-60s  " "${MSG}"

	set +e
	ip netns exec "${NS0}" "${PING}" -w 5 -c 1 "${IP}" > /dev/null 2>&1
	RET=$?
	set -e

	if [ "${RET}" -eq 0 ]; then
		printf "[ OK ]\n"
	else
		ERR=1
		printf "[FAIL]\n"
		if [ "${PAUSE_ON_FAIL}" = "yes" ]; then
			printf "\nHit enter to continue, 'q' to quit\n"
			read a
			if [ "$a" = "q" ]; then
				exit 1
			fi
		fi
	fi
}

# Run reachability tests
#
# Parameters:
#
#   * $1: human readable string describing the underlay protocol.
#
# $IPV4, $IPV6, $MPLS_UC and $MULTIPROTO are inherited from the calling
# function.
#
ping_test()
{
	local UNDERLAY="$1"; readonly UNDERLAY
	local MODE
	local MSG

	if [ "${MULTIPROTO}" = "multiproto" ]; then
		MODE=" (multiproto mode)"
	else
		MODE=""
	fi

	if [ $IPV4 ]; then
		ping_test_one "ping" "192.0.2.103" "IPv4 packets over ${UNDERLAY}${MODE}"
	fi
	if [ $IPV6 ]; then
		ping_test_one "${PING6}" "2001:db8::103" "IPv6 packets over ${UNDERLAY}${MODE}"
	fi
	if [ $MPLS_UC ]; then
		ping_test_one "${PING6}" "2001:db8::203" "Unicast MPLS packets over ${UNDERLAY}${MODE}"
	fi
}

# Set up a bareudp overlay and run reachability tests over IPv4 and IPv6
#
# Parameters:
#
#   * $1: the packet type (protocol) to be handled by bareudp,
#   * $2: a flag to activate or deactivate bareudp's "multiproto" mode.
#
test_overlay()
{
	local ETHERTYPE="$1"; readonly ETHERTYPE
	local MULTIPROTO="$2"; readonly MULTIPROTO
	local IPV4
	local IPV6
	local MPLS_UC

	case "${ETHERTYPE}" in
		"ipv4")
			IPV4="ipv4"
			if [ "${MULTIPROTO}" = "multiproto" ]; then
				IPV6="ipv6"
			else
				IPV6=""
			fi
			MPLS_UC=""
			;;
		"ipv6")
			IPV6="ipv6"
			IPV4=""
			MPLS_UC=""
			;;
		"mpls_uc")
			MPLS_UC="mpls_uc"
			IPV4=""
			IPV6=""
			;;
		*)
			exit 1
			;;
	esac
	readonly IPV4
	readonly IPV6
	readonly MPLS_UC

	# Create the bareudp devices in the intermediate namespaces
	ip -netns "${NS1}" link add name bareudp_ns1 up type bareudp dstport 6635 ethertype "${ETHERTYPE}" "${MULTIPROTO}"
	ip -netns "${NS2}" link add name bareudp_ns2 up type bareudp dstport 6635 ethertype "${ETHERTYPE}" "${MULTIPROTO}"

	# IPv4 over UDPv4
	if [ $IPV4 ]; then
		# Encapsulation instructions for bareudp over IPv4
		tc -netns "${NS1}" filter add dev veth10 ingress protocol ipv4         \
			flower dst_ip 192.0.2.103/32                                   \
			action tunnel_key set src_ip 192.0.2.21 dst_ip 192.0.2.22 id 0 \
			action mirred egress redirect dev bareudp_ns1
		tc -netns "${NS2}" filter add dev veth23 ingress protocol ipv4         \
			flower dst_ip 192.0.2.100/32                                   \
			action tunnel_key set src_ip 192.0.2.22 dst_ip 192.0.2.21 id 0 \
			action mirred egress redirect dev bareudp_ns2
	fi

	# IPv6 over UDPv4
	if [ $IPV6 ]; then
		# Encapsulation instructions for bareudp over IPv4
		tc -netns "${NS1}" filter add dev veth10 ingress protocol ipv6         \
			flower dst_ip 2001:db8::103/128                                \
			action tunnel_key set src_ip 192.0.2.21 dst_ip 192.0.2.22 id 0 \
			action mirred egress redirect dev bareudp_ns1
		tc -netns "${NS2}" filter add dev veth23 ingress protocol ipv6         \
			flower dst_ip 2001:db8::100/128                                \
			action tunnel_key set src_ip 192.0.2.22 dst_ip 192.0.2.21 id 0 \
			action mirred egress redirect dev bareudp_ns2
	fi

	# MPLS (unicast) over UDPv4
	if [ $MPLS_UC ]; then
		ip netns exec "${NS1}" sysctl -qw net.mpls.conf.bareudp_ns1.input=1
		ip netns exec "${NS2}" sysctl -qw net.mpls.conf.bareudp_ns2.input=1

		# Encapsulation instructions for bareudp over IPv4
		tc -netns "${NS1}" filter add dev veth10 ingress protocol mpls_uc      \
			flower mpls_label 203                                          \
			action tunnel_key set src_ip 192.0.2.21 dst_ip 192.0.2.22 id 0 \
			action mirred egress redirect dev bareudp_ns1
		tc -netns "${NS2}" filter add dev veth23 ingress protocol mpls_uc      \
			flower mpls_label 200                                          \
			action tunnel_key set src_ip 192.0.2.22 dst_ip 192.0.2.21 id 0 \
			action mirred egress redirect dev bareudp_ns2
	fi

	# Test IPv4 underlay
	ping_test "UDPv4"

	# Cleanup bareudp encapsulation instructions, as they were specific to
	# the IPv4 underlay, before setting up and testing the IPv6 underlay
	tc -netns "${NS1}" filter delete dev veth10 ingress
	tc -netns "${NS2}" filter delete dev veth23 ingress

	# IPv4 over UDPv6
	if [ $IPV4 ]; then
		# New encapsulation instructions for bareudp over IPv6
		tc -netns "${NS1}" filter add dev veth10 ingress protocol ipv4             \
			flower dst_ip 192.0.2.103/32                                       \
			action tunnel_key set src_ip 2001:db8::21 dst_ip 2001:db8::22 id 0 \
			action mirred egress redirect dev bareudp_ns1
		tc -netns "${NS2}" filter add dev veth23 ingress protocol ipv4             \
			flower dst_ip 192.0.2.100/32                                       \
			action tunnel_key set src_ip 2001:db8::22 dst_ip 2001:db8::21 id 0 \
			action mirred egress redirect dev bareudp_ns2
	fi

	# IPv6 over UDPv6
	if [ $IPV6 ]; then
		# New encapsulation instructions for bareudp over IPv6
		tc -netns "${NS1}" filter add dev veth10 ingress protocol ipv6             \
			flower dst_ip 2001:db8::103/128                                    \
			action tunnel_key set src_ip 2001:db8::21 dst_ip 2001:db8::22 id 0 \
			action mirred egress redirect dev bareudp_ns1
		tc -netns "${NS2}" filter add dev veth23 ingress protocol ipv6             \
			flower dst_ip 2001:db8::100/128                                    \
			action tunnel_key set src_ip 2001:db8::22 dst_ip 2001:db8::21 id 0 \
			action mirred egress redirect dev bareudp_ns2
	fi

	# MPLS (unicast) over UDPv6
	if [ $MPLS_UC ]; then
		# New encapsulation instructions for bareudp over IPv6
		tc -netns "${NS1}" filter add dev veth10 ingress protocol mpls_uc          \
			flower mpls_label 203                                              \
			action tunnel_key set src_ip 2001:db8::21 dst_ip 2001:db8::22 id 0 \
			action mirred egress redirect dev bareudp_ns1
		tc -netns "${NS2}" filter add dev veth23 ingress protocol mpls_uc          \
			flower mpls_label 200                                              \
			action tunnel_key set src_ip 2001:db8::22 dst_ip 2001:db8::21 id 0 \
			action mirred egress redirect dev bareudp_ns2
	fi

	# Test IPv6 underlay
	ping_test "UDPv6"

	tc -netns "${NS1}" filter delete dev veth10 ingress
	tc -netns "${NS2}" filter delete dev veth23 ingress
	ip -netns "${NS1}" link delete bareudp_ns1
	ip -netns "${NS2}" link delete bareudp_ns2
}

check_features()
{
	ip link help 2>&1 | grep -q bareudp
	if [ $? -ne 0 ]; then
		echo "Missing bareudp support in iproute2" >&2
		exit_cleanup
	fi

	# Use ping6 on systems where ping doesn't handle IPv6
	ping -w 1 -c 1 ::1 > /dev/null 2>&1 || PING6="ping6"
}

usage()
{
	echo "Usage: $0 [-p]"
	exit 1
}

while getopts :p o
do
	case $o in
		p) PAUSE_ON_FAIL="yes";;
		*) usage;;
	esac
done

check_features

# Create namespaces before setting up the exit trap.
# Otherwise, exit_cleanup_all() could delete namespaces that were not created
# by this script.
create_namespaces

set -e
trap exit_cleanup_all EXIT

setup_underlay
setup_overlay_ipv4
setup_overlay_ipv6
setup_overlay_mpls

test_overlay ipv4 nomultiproto
test_overlay ipv6 nomultiproto
test_overlay ipv4 multiproto
test_overlay mpls_uc nomultiproto

if [ "${ERR}" -eq 1 ]; then
	echo "Some tests failed." >&2
else
	ERR=0
fi
