#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# author: Andrea Mayer <andrea.mayer@uniroma2.it>
# author: Paolo Lungaroni <paolo.lungaroni@uniroma2.it>
#
# This script is designed for testing the support of NEXT-C-SID flavor for SRv6
# End.X behavior.
# A basic knowledge of SRv6 architecture [1] and of the compressed SID approach
# [2] is assumed for the reader.
#
# The network topology used in the selftest is depicted hereafter, composed of
# two hosts and four routers. Hosts hs-1 and hs-2 are connected through an
# IPv4/IPv6 L3 VPN service, offered by routers rt-1, rt-2, rt-3 and rt-4 using
# the NEXT-C-SID flavor. The key components for such VPNs are:
#
#    i) The SRv6 H.Encaps/H.Encaps.Red behaviors [1] apply SRv6 Policies on
#       traffic received by connected hosts, initiating the VPN tunnel;
#
#   ii) The SRv6 End.X behavior [1] (Endpoint with L3 cross connect) is a
#       variant of SRv6 End behavior. It advances the active SID in the SID
#       List carried by the SRH and forwards the packet to an L3 adjacency;
#
#  iii) The NEXT-C-SID mechanism [2] offers the possibility of encoding several
#       SRv6 segments within a single 128-bit SID address, referred to as a
#       Compressed SID (C-SID) container. In this way, the length of the SID
#       List can be drastically reduced.
#       The NEXT-C-SID is provided as a "flavor" of the SRv6 End.X behavior
#       which advances the current C-SID (i.e. the Locator-Node Function defined
#       in [2]) with the next one carried in the Argument, if available.
#       When no more C-SIDs are available in the Argument, the SRv6 End.X
#       behavior will apply the End.X function selecting the next SID in the SID
#       List;
#
#   iv) The SRv6 End.DT46 behavior [1] is used for removing the SRv6 Policy and,
#       thus, it terminates the VPN tunnel. Such a behavior is capable of
#       handling, at the same time, both tunneled IPv4 and IPv6 traffic.
#
# [1] https://datatracker.ietf.org/doc/html/rfc8986
# [2] https://datatracker.ietf.org/doc/html/draft-ietf-spring-srv6-srh-compression
#
#
#               cafe::1                      cafe::2
#              10.0.0.1                     10.0.0.2
#             +--------+                   +--------+
#             |        |                   |        |
#             |  hs-1  |                   |  hs-2  |
#             |        |                   |        |
#             +---+----+                   +----+---+
#    cafe::/64    |                             |      cafe::/64
#  10.0.0.0/24    |                             |    10.0.0.0/24
#             +---+----+                   +----+---+
#             |        |  fcf0:0:1:2::/64  |        |
#             |  rt-1  +-------------------+  rt-2  |
#             |        |                   |        |
#             +---+----+                   +----+---+
#                 |      .               .      |
#                 |  fcf0:0:1:3::/64   .        |
#                 |          .       .          |
#                 |            .   .            |
# fcf0:0:1:4::/64 |              .              | fcf0:0:2:3::/64
#                 |            .   .            |
#                 |          .       .          |
#                 |  fcf0:0:2:4::/64   .        |
#                 |      .               .      |
#             +---+----+                   +----+---+
#             |        |                   |        |
#             |  rt-4  +-------------------+  rt-3  |
#             |        |  fcf0:0:3:4::/64  |        |
#             +---+----+                   +----+---+
#
# Every fcf0:0:x:y::/64 network interconnects the SRv6 routers rt-x with rt-y in
# the selftest network.
#
# In addition, every router interface connecting rt-x to rt-y is assigned an
# IPv6 link-local address fe80::x:y/64.
#
# Local SID/C-SID table
# =====================
#
# Each SRv6 router is configured with a Local SID/C-SID table in which
# SIDs/C-SIDs are stored. Considering an SRv6 router rt-x, SIDs/C-SIDs are
# configured in the Local SID/C-SIDs table as follows:
#
#   Local SID/C-SID table for SRv6 router rt-x
#   +-----------------------------------------------------------+
#   |fcff:x::d46 is associated with the non-compressed SRv6     |
#   |   End.DT46 behavior                                       |
#   +-----------------------------------------------------------+
#   |fcbb:0:0x00::/48 is associated with the NEXT-C-SID flavor  |
#   |   of SRv6 End.X behavior                                  |
#   +-----------------------------------------------------------+
#   |fcbb:0:0x00:d46::/64 is associated with the SRv6 End.DT46  |
#   |   behavior when NEXT-C-SID compression is turned on       |
#   +-----------------------------------------------------------+
#
# The fcff::/16 prefix is reserved for implementing SRv6 services with regular
# (non compressed) SIDs. Reachability of SIDs is ensured by proper configuration
# of the IPv6 routing tables in the routers.
# Similarly, the fcbb:0::/32 prefix is reserved for implementing SRv6 VPN
# services leveraging the NEXT-C-SID compression mechanism. Indeed, the
# fcbb:0::/32 is used for encoding the Locator-Block while the Locator-Node
# Function is encoded with 16 bits.
#
# Incoming traffic classification and application of SRv6 Policies
# ================================================================
#
# An SRv6 ingress router applies different SRv6 Policies to the traffic received
# from a connected host, considering the IPv4 or IPv6 destination address.
# SRv6 policy enforcement consists of encapsulating the received traffic into a
# new IPv6 packet with a given SID List contained in the SRH.
# When the SID List contains only one SID, the SRH could be omitted completely
# and that SID is stored directly in the IPv6 Destination Address (DA) (this is
# called "reduced" encapsulation).
#
# Test cases for NEXT-C-SID
# =========================
#
# We consider two test cases for NEXT-C-SID: i) single SID and ii) double SID.
#
# In the single SID test case we have a number of segments that are all
# contained in a single Compressed SID (C-SID) container. Therefore the
# resulting SID List has only one SID. Using the reduced encapsulation format
# this will result in a packet with no SRH.
#
# In the double SID test case we have one segment carried in a Compressed SID
# (C-SID) container, followed by a regular (non compressed) SID. The resulting
# SID List has two segments and it is possible to test the advance to the next
# SID when all the C-SIDs in a C-SID container have been processed. Using the
# reduced encapsulation format this will result in a packet with an SRH
# containing 1 segment.
#
# For the single SID test case, we use the IPv6 addresses of hs-1 and hs-2, for
# the double SID test case, we use their IPv4 addresses. This is only done to
# simplify the test setup and avoid adding other hosts or multiple addresses on
# the same interface of a host.
#
# Traffic from hs-1 to hs-2
# -------------------------
#
# Packets generated from hs-1 and directed towards hs-2 are handled by rt-1
# which applies the SRv6 Policies as follows:
#
#   i) IPv6 DA=cafe::2, H.Encaps.Red with SID List=fcbb:0:0300:0200:d46::
#  ii) IPv4 DA=10.0.0.2, H.Encaps.Red with SID List=fcbb:0:0300::,fcff:2::d46
#
# ### i) single SID
#
# The router rt-1 is configured to enforce the given Policy through the SRv6
# H.Encaps.Red behavior which avoids the presence of the SRH at all, since it
# pushes the single SID directly in the IPv6 DA. Such a SID encodes a whole
# C-SID container carrying several C-SIDs (e.g. 0300, 0200, etc).
#
# As the packet reaches the router rt-3, the enabled NEXT-C-SID SRv6 End.X
# behavior (associated with fcbb:0:0300::/48) is triggered. This behavior
# analyzes the IPv6 DA and checks whether the Argument of the C-SID container
# is zero or not. In this case, the Argument is *NOT* zero and the IPv6 DA is
# updated as follows:
#
# +-----------------------------------------------------------------+
# | Before applying the rt-3 enabled NEXT-C-SID SRv6 End.X behavior |
# +-----------------------------------------------------------------+
# |                            +---------- Argument                 |
# |                     vvvvvvvvvv                                  |
# | IPv6 DA fcbb:0:0300:0200:d46::                                  |
# |                ^^^^    <-- shifting                             |
# |                  |                                              |
# |          Locator-Node Function                                  |
# +-----------------------------------------------------------------+
# | After applying the rt-3 enabled NEXT-C-SID SRv6 End.X behavior  |
# +-----------------------------------------------------------------+
# |                          +---------- Argument                   |
# |                    vvvvvv                                       |
# | IPv6 DA fcbb:0:0200:d46::                                       |
# |                ^^^^                                             |
# |                  |                                              |
# |          Locator-Node Function                                  |
# +-----------------------------------------------------------------+
#
# After having applied the enabled NEXT-C-SID SRv6 End.X behavior, the packet
# is sent to rt-4 node using the L3 adjacency address fcf0:0:3:4::4.
#
# The node rt-4 performs a plain IPv6 forward to the rt-2 router according to
# its Local SID table and using the IPv6 DA fcbb:0:0200:d46:: .
#
# The router rt-2 is configured for decapsulating the inner IPv6 packet and,
# for this reason, it applies the SRv6 End.DT46 behavior on the received
# packet. It is worth noting that the SRv6 End.DT46 behavior does not require
# the presence of the SRH: it is fully capable to operate properly on
# IPv4/IPv6-in-IPv6 encapsulations.
# At the end of the decap operation, the packet is sent to the host hs-2.
#
# ### ii) double SID
#
# The router rt-1 is configured to enforce the given Policy through the SRv6
# H.Encaps.Red. As a result, the first SID fcbb:0:0300:: is stored into the
# IPv6 DA, while the SRH pushed into the packet is made of only one SID, i.e.
# fcff:2::d46. Hence, the packet sent by hs-1 to hs-2 is encapsulated in an
# outer IPv6 header plus the SRH.
#
# As the packet reaches the node rt-3, the router applies the enabled NEXT-C-SID
# SRv6 End.X behavior.
#
# +-----------------------------------------------------------------+
# | Before applying the rt-3 enabled NEXT-C-SID SRv6 End.X behavior |
# +-----------------------------------------------------------------+
# |                      +---------- Argument                       |
# |                      vvvv (Argument is all filled with zeros)   |
# | IPv6 DA fcbb:0:0300::                                           |
# |                ^^^^                                             |
# |                  |                                              |
# |          Locator-Node Function                                  |
# +-----------------------------------------------------------------+
# | After applying the rt-3 enabled NEXT-C-SID SRv6 End.X behavior  |
# +-----------------------------------------------------------------+
# |                                                                 |
# | IPv6 DA fcff:2::d46                                             |
# |         ^^^^^^^^^^^                                             |
# |              |                                                  |
# |        SID copied from the SID List contained in the SRH        |
# +-----------------------------------------------------------------+
#
# Since the Argument of the C-SID container is zero, the behavior can not
# update the Locator-Node function with the next C-SID carried in the Argument
# itself. Thus, the enabled NEXT-C-SID SRv6 End.X behavior operates as the
# traditional End.X behavior: it updates the IPv6 DA by copying the next
# available SID in the SID List carried by the SRH. Next, the packet is
# forwarded to the rt-4 node using the L3 adjacency fcf0:3:4::4 previously
# configured for this behavior.
#
# The node rt-4 performs a plain IPv6 forward to the rt-2 router according to
# its Local SID table and using the IPv6 DA fcff:2::d46.
#
# Once the packet is received by rt-2, the router decapsulates the inner IPv4
# packet using the SRv6 End.DT46 behavior (associated with the SID fcff:2::d46)
# and sends it to the host hs-2.
#
# Traffic from hs-2 to hs-1
# -------------------------
#
# Packets generated from hs-2 and directed towards hs-1 are handled by rt-2
# which applies the SRv6 Policies as follows:
#
#   i) IPv6 DA=cafe::1, SID List=fcbb:0:0400:0100:d46::
#  ii) IPv4 DA=10.0.0.1, SID List=fcbb:0:0300::,fcff:1::d46
#
# ### i) single SID
#
# The node hs-2 sends an IPv6 packet directed to node hs-1. The router rt-2 is
# directly connected to hs-2 and receives the packet. Rt-2 applies the
# H.Encap.Red behavior with policy i) described above. Since there is only one
# SID, the SRH header is omitted and the policy is inserted directly into the DA
# of IPv6 packet.
#
# The packet reaches the router rt-4 and the enabled NEXT-C-SID SRv6 End.X
# behavior (associated with fcbb:0:0400::/48) is triggered. This behavior
# analyzes the IPv6 DA and checks whether the Argument of the C-SID container
# is zero or not. The Argument is *NOT* zero and the C-SID in the IPv6 DA is
# advanced. At this point, the current IPv6 DA is fcbb:0:0100:d46:: .
# The enabled NEXT-C-SID SRv6 End.X behavior is configured with the L3 adjacency
# fcf0:0:1:4::1, used to route traffic to the rt-1 node.
#
# The router rt-1 is configured for decapsulating the inner packet. It applies
# the SRv6 End.DT46 behavior on the received packet. Decapsulation does not
# require the presence of the SRH. At the end of the decap operation, the packet
# is sent to the host hs-1.
#
# ### ii) double SID
#
# The router rt-2 is configured to enforce the given Policy through the SRv6
# H.Encaps.Red. As a result, the first SID fcbb:0:0300:: is stored into the
# IPv6 DA, while the SRH pushed into the packet is made of only one SID, i.e.
# fcff:1::d46. Hence, the packet sent by hs-2 to hs-1 is encapsulated in an
# outer IPv6 header plus the SRH.
#
# As the packet reaches the node rt-3, the enabled NEXT-C-SID SRv6 End.X
# behavior bound to the SID fcbb:0:0300::/48 is triggered.
# Since the Argument of the C-SID container is zero, the behavior can not
# update the Locator-Node function with the next C-SID carried in the Argument
# itself. Thus, the enabled NEXT-C-SID SRv6 End-X behavior operates as the
# traditional End.X behavior: it updates the IPv6 DA by copying the next
# available SID in the SID List carried by the SRH. After that, the packet is
# forwarded to the rt-4 node using the L3 adjacency (fcf0:3:4::4) previously
# configured for this behavior.
#
# The node rt-4 performs a plain IPv6 forward to the rt-1 router according to
# its Local SID table, considering the IPv6 DA fcff:1::d46.
#
# Once the packet is received by rt-1, the router decapsulates the inner IPv4
# packet using the SRv6 End.DT46 behavior (associated with the SID fcff:1::d46)
# and sends it to the host hs-1.

source lib.sh

readonly DUMMY_DEVNAME="dum0"
readonly VRF_TID=100
readonly VRF_DEVNAME="vrf-${VRF_TID}"
readonly RT2HS_DEVNAME="veth-t${VRF_TID}"
readonly LOCALSID_TABLE_ID=90
readonly IPv6_RT_NETWORK=fcf0:0
readonly IPv6_HS_NETWORK=cafe
readonly IPv4_HS_NETWORK=10.0.0
readonly VPN_LOCATOR_SERVICE=fcff
readonly DT46_FUNC=0d46
readonly HEADEND_ENCAP="encap.red"

# do not add ':' as separator
readonly LCBLOCK_ADDR=fcbb0000
readonly LCBLOCK_BLEN=32
# do not add ':' as separator
readonly LCNODEFUNC_FMT="0%d00"
readonly LCNODEFUNC_BLEN=16

readonly LCBLOCK_NODEFUNC_BLEN=$((LCBLOCK_BLEN + LCNODEFUNC_BLEN))

readonly CSID_CNTR_PREFIX="dead:beaf::/32"
# ID of the router used for testing the C-SID container cfgs
readonly CSID_CNTR_RT_ID_TEST=1
# Routing table used for testing the C-SID container cfgs
readonly CSID_CNTR_RT_TABLE=91

# C-SID container configurations to be tested
#
# An entry of the array is defined as "a,b,c" where:
# - 'a' and 'b' elements represent respectively the Locator-Block length
#   (lblen) in bits and the Locator-Node Function length (nflen) in bits.
#   'a' and 'b' can be set to default values using the placeholder "d" which
#   indicates the default kernel values (32 for lblen and 16 for nflen);
#   otherwise, any numeric value is accepted;
# - 'c' indicates whether the C-SID configuration provided by the values 'a'
#   and 'b' should be considered valid ("y") or invalid ("n").
declare -ra CSID_CONTAINER_CFGS=(
	"d,d,y"
	"d,16,y"
	"16,d,y"
	"16,32,y"
	"32,16,y"
	"48,8,y"
	"8,48,y"
	"d,0,n"
	"0,d,n"
	"32,0,n"
	"0,32,n"
	"17,d,n"
	"d,17,n"
	"120,16,n"
	"16,120,n"
	"0,128,n"
	"128,0,n"
	"130,0,n"
	"0,130,n"
	"0,0,n"
)

PING_TIMEOUT_SEC=4
PAUSE_ON_FAIL=${PAUSE_ON_FAIL:=no}

# IDs of routers and hosts are initialized during the setup of the testing
# network
ROUTERS=''
HOSTS=''

SETUP_ERR=1

ret=${ksft_skip}
nsuccess=0
nfail=0

log_test()
{
	local rc="$1"
	local expected="$2"
	local msg="$3"

	if [ "${rc}" -eq "${expected}" ]; then
		nsuccess=$((nsuccess+1))
		printf "\n    TEST: %-60s  [ OK ]\n" "${msg}"
	else
		ret=1
		nfail=$((nfail+1))
		printf "\n    TEST: %-60s  [FAIL]\n" "${msg}"
		if [ "${PAUSE_ON_FAIL}" = "yes" ]; then
			echo
			echo "hit enter to continue, 'q' to quit"
			read a
			[ "$a" = "q" ] && exit 1
		fi
	fi
}

print_log_test_results()
{
	printf "\nTests passed: %3d\n" "${nsuccess}"
	printf "Tests failed: %3d\n"   "${nfail}"

	# when a test fails, the value of 'ret' is set to 1 (error code).
	# Conversely, when all tests are passed successfully, the 'ret' value
	# is set to 0 (success code).
	if [ "${ret}" -ne 1 ]; then
		ret=0
	fi
}

log_section()
{
	echo
	echo "################################################################################"
	echo "TEST SECTION: $*"
	echo "################################################################################"
}

test_command_or_ksft_skip()
{
	local cmd="$1"

	if [ ! -x "$(command -v "${cmd}")" ]; then
		echo "SKIP: Could not run test without \"${cmd}\" tool";
		exit "${ksft_skip}"
	fi
}

get_rtname()
{
	local rtid="$1"

	echo "rt_${rtid}"
}

get_hsname()
{
	local hsid="$1"

	echo "hs_${hsid}"
}

create_router()
{
	local rtid="$1"
	local nsname

	nsname="$(get_rtname "${rtid}")"
	setup_ns "${nsname}"

	eval nsname=\${$(get_rtname "${rtid}")}
	ip netns exec "${nsname}" sysctl -wq net.ipv6.conf.all.accept_dad=0
	ip netns exec "${nsname}" sysctl -wq net.ipv6.conf.default.accept_dad=0
	ip netns exec "${nsname}" sysctl -wq net.ipv6.conf.all.forwarding=1
	ip netns exec "${nsname}" sysctl -wq net.ipv4.ip_forward=1
}

create_host()
{
	local hsid="$1"
	local nsname

	nsname="$(get_hsname "${hsid}")"
	setup_ns "${nsname}"
}

cleanup()
{
	cleanup_all_ns
	# check whether the setup phase was completed successfully or not. In
	# case of an error during the setup phase of the testing environment,
	# the selftest is considered as "skipped".
	if [ "${SETUP_ERR}" -ne 0 ]; then
		echo "SKIP: Setting up the testing environment failed"
		exit "${ksft_skip}"
	fi

	exit "${ret}"
}

add_link_rt_pairs()
{
	local rt="$1"
	local rt_neighs="$2"
	local neigh
	local nsname
	local neigh_nsname

	eval nsname=\${$(get_rtname "${rt}")}

	for neigh in ${rt_neighs}; do
		eval neigh_nsname=\${$(get_rtname "${neigh}")}

		ip link add "veth-rt-${rt}-${neigh}" netns "${nsname}" \
			type veth peer name "veth-rt-${neigh}-${rt}" \
			netns "${neigh_nsname}"
	done
}

get_network_prefix()
{
	local rt="$1"
	local neigh="$2"
	local p="${rt}"
	local q="${neigh}"

	if [ "${p}" -gt "${q}" ]; then
		p="${q}"; q="${rt}"
	fi

	echo "${IPv6_RT_NETWORK}:${p}:${q}"
}

# Setup the basic networking for the routers
setup_rt_networking()
{
	local rt="$1"
	local rt_neighs="$2"
	local nsname
	local net_prefix
	local devname
	local neigh

	eval nsname=\${$(get_rtname "${rt}")}

	for neigh in ${rt_neighs}; do
		devname="veth-rt-${rt}-${neigh}"

		net_prefix="$(get_network_prefix "${rt}" "${neigh}")"

		ip -netns "${nsname}" addr \
			add "${net_prefix}::${rt}/64" dev "${devname}" nodad

		ip -netns "${nsname}" addr \
			add "fe80::${rt}:${neigh}/64" dev "${devname}" nodad

		ip -netns "${nsname}" link set "${devname}" up
	done

        ip -netns "${nsname}" link add "${DUMMY_DEVNAME}" type dummy

        ip -netns "${nsname}" link set "${DUMMY_DEVNAME}" up
	ip -netns "${nsname}" link set lo up
}

# build an ipv6 prefix/address based on the input string
# Note that the input string does not contain ':' and '::' which are considered
# to be implicit.
# e.g.:
#  - input:  fbcc00000400300
#  - output: fbcc:0000:0400:0300:0000:0000:0000:0000
#                                ^^^^^^^^^^^^^^^^^^^
#                              fill the address with 0s
build_ipv6_addr()
{
	local addr="$1"
	local out=""
	local strlen="${#addr}"
	local padn
	local i

	# add ":" every 4 digits (16 bits)
	for (( i = 0; i < strlen; i++ )); do
		if (( i > 0 && i < 32 && (i % 4) == 0 )); then
			out="${out}:"
		fi

		out="${out}${addr:$i:1}"
	done

	# fill the remaining bits of the address with 0s
	padn=$((32 - strlen))
	for (( i = padn; i > 0; i-- )); do
		if (( i > 0 && i < 32 && (i % 4) == 0 )); then
			out="${out}:"
		fi

		out="${out}0"
	done

	printf "${out}"
}

build_csid()
{
	local nodeid="$1"

	printf "${LCNODEFUNC_FMT}" "${nodeid}"
}

build_lcnode_func_prefix()
{
	local nodeid="$1"
	local lcnodefunc
	local prefix
	local out

	lcnodefunc="$(build_csid "${nodeid}")"
	prefix="$(build_ipv6_addr "${LCBLOCK_ADDR}${lcnodefunc}")"

	out="${prefix}/${LCBLOCK_NODEFUNC_BLEN}"

	echo "${out}"
}

set_end_x_nextcsid()
{
	local rt="$1"
	local adj="$2"

	eval nsname=\${$(get_rtname "${rt}")}
	net_prefix="$(get_network_prefix "${rt}" "${adj}")"
	lcnode_func_prefix="$(build_lcnode_func_prefix "${rt}")"

	# enabled NEXT-C-SID SRv6 End.X behavior (note that "dev" is the dummy
	# dum0 device chosen for the sake of simplicity).
	ip -netns "${nsname}" -6 route \
		replace "${lcnode_func_prefix}" \
		table "${LOCALSID_TABLE_ID}" \
		encap seg6local action End.X nh6 "${net_prefix}::${adj}" \
		flavors next-csid lblen "${LCBLOCK_BLEN}" \
		nflen "${LCNODEFUNC_BLEN}" dev "${DUMMY_DEVNAME}"
}

set_end_x_ll_nextcsid()
{
	local rt="$1"
	local adj="$2"

	eval nsname=\${$(get_rtname "${rt}")}
	lcnode_func_prefix="$(build_lcnode_func_prefix "${rt}")"
	nh6_ll_addr="fe80::${adj}:${rt}"
	oifname="veth-rt-${rt}-${adj}"

	# enabled NEXT-C-SID SRv6 End.X behavior via an IPv6 link-local nexthop
	# address (note that "dev" is the dummy dum0 device chosen for the sake
	# of simplicity).
	ip -netns "${nsname}" -6 route \
		replace "${lcnode_func_prefix}" \
		table "${LOCALSID_TABLE_ID}" \
		encap seg6local action End.X nh6 "${nh6_ll_addr}" \
		oif "${oifname}" flavors next-csid lblen "${LCBLOCK_BLEN}" \
		nflen "${LCNODEFUNC_BLEN}" dev "${DUMMY_DEVNAME}"
}

set_underlay_sids_reachability()
{
	local rt="$1"
	local rt_neighs="$2"

	eval nsname=\${$(get_rtname "${rt}")}

	for neigh in ${rt_neighs}; do
		devname="veth-rt-${rt}-${neigh}"

		net_prefix="$(get_network_prefix "${rt}" "${neigh}")"

		# set underlay network routes for SIDs reachability
		ip -netns "${nsname}" -6 route \
			replace "${VPN_LOCATOR_SERVICE}:${neigh}::/32" \
			table "${LOCALSID_TABLE_ID}" \
			via "${net_prefix}::${neigh}" dev "${devname}"

		# set the underlay network for C-SIDs reachability
		lcnode_func_prefix="$(build_lcnode_func_prefix "${neigh}")"

		ip -netns "${nsname}" -6 route \
			replace "${lcnode_func_prefix}" \
			table "${LOCALSID_TABLE_ID}" \
			via "${net_prefix}::${neigh}" dev "${devname}"
	done
}

# Setup local SIDs for an SRv6 router
setup_rt_local_sids()
{
	local rt="$1"
	local rt_neighs="$2"
	local net_prefix
	local devname
	local nsname
	local neigh
	local lcnode_func_prefix
	local lcblock_prefix

	eval nsname=\${$(get_rtname "${rt}")}

        set_underlay_sids_reachability "${rt}" "${rt_neighs}"

	# all SIDs for VPNs start with a common locator. Routes and SRv6
	# Endpoint behavior instances are grouped together in the 'localsid'
	# table.
	ip -netns "${nsname}" -6 rule \
		add to "${VPN_LOCATOR_SERVICE}::/16" \
		lookup "${LOCALSID_TABLE_ID}" prio 999

	# common locator block for NEXT-C-SIDS compression mechanism.
	lcblock_prefix="$(build_ipv6_addr "${LCBLOCK_ADDR}")"
	ip -netns "${nsname}" -6 rule \
		add to "${lcblock_prefix}/${LCBLOCK_BLEN}" \
		lookup "${LOCALSID_TABLE_ID}" prio 999
}

# build and install the SRv6 policy into the ingress SRv6 router as well as the
# decap SID in the egress one.
# args:
#  $1 - src host (evaluate automatically the ingress router)
#  $2 - dst host (evaluate automatically the egress router)
#  $3 - SRv6 routers configured for steering traffic (End.X behaviors)
#  $4 - single SID or double SID
#  $5 - traffic type (IPv6 or IPv4)
__setup_l3vpn()
{
	local src="$1"
	local dst="$2"
	local end_rts="$3"
	local mode="$4"
	local traffic="$5"
	local nsname
	local policy
	local container
	local decapsid
	local lcnfunc
	local dt
	local n
	local rtsrc_nsname
	local rtdst_nsname

	eval rtsrc_nsname=\${$(get_rtname "${src}")}
	eval rtdst_nsname=\${$(get_rtname "${dst}")}

	container="${LCBLOCK_ADDR}"

	# build first SID (C-SID container)
	for n in ${end_rts}; do
		lcnfunc="$(build_csid "${n}")"

		container="${container}${lcnfunc}"
	done

	if [ "${mode}" -eq 1 ]; then
		# single SID policy
		dt="$(build_csid "${dst}")${DT46_FUNC}"
		container="${container}${dt}"
		# build the full ipv6 address for the container
		policy="$(build_ipv6_addr "${container}")"

		# build the decap SID used in the decap node
		container="${LCBLOCK_ADDR}${dt}"
		decapsid="$(build_ipv6_addr "${container}")"
	else
		# double SID policy
		decapsid="${VPN_LOCATOR_SERVICE}:${dst}::${DT46_FUNC}"

		policy="$(build_ipv6_addr "${container}"),${decapsid}"
	fi

	# apply encap policy
	if [ "${traffic}" -eq 6 ]; then
		ip -netns "${rtsrc_nsname}" -6 route \
			add "${IPv6_HS_NETWORK}::${dst}" vrf "${VRF_DEVNAME}" \
			encap seg6 mode "${HEADEND_ENCAP}" segs "${policy}" \
			dev "${VRF_DEVNAME}"

		ip -netns "${rtsrc_nsname}" -6 neigh \
			add proxy "${IPv6_HS_NETWORK}::${dst}" \
			dev "${RT2HS_DEVNAME}"
	else
		# "dev" must be different from the one where the packet is
		# received, otherwise the proxy arp does not work.
		ip -netns "${rtsrc_nsname}" -4 route \
			add "${IPv4_HS_NETWORK}.${dst}" vrf "${VRF_DEVNAME}" \
			encap seg6 mode "${HEADEND_ENCAP}" segs "${policy}" \
			dev "${VRF_DEVNAME}"
	fi

	# apply decap
	# Local End.DT46 behavior (decap)
	ip -netns "${rtdst_nsname}" -6 route \
		add "${decapsid}" \
		table "${LOCALSID_TABLE_ID}" \
		encap seg6local action End.DT46 vrftable "${VRF_TID}" \
		dev "${VRF_DEVNAME}"
}

# see __setup_l3vpn()
setup_ipv4_vpn_2sids()
{
	__setup_l3vpn "$1" "$2" "$3" 2 4
}

# see __setup_l3vpn()
setup_ipv6_vpn_1sid()
{
	__setup_l3vpn "$1" "$2" "$3" 1 6
}

setup_hs()
{
	local hs="$1"
	local rt="$2"
	local hsname
	local rtname

	eval hsname=\${$(get_hsname "${hs}")}
	eval rtname=\${$(get_rtname "${rt}")}

	ip netns exec "${hsname}" sysctl -wq net.ipv6.conf.all.accept_dad=0
	ip netns exec "${hsname}" sysctl -wq net.ipv6.conf.default.accept_dad=0

	ip -netns "${hsname}" link add veth0 type veth \
		peer name "${RT2HS_DEVNAME}" netns "${rtname}"

	ip -netns "${hsname}" addr \
		add "${IPv6_HS_NETWORK}::${hs}/64" dev veth0 nodad
	ip -netns "${hsname}" addr add "${IPv4_HS_NETWORK}.${hs}/24" dev veth0

	ip -netns "${hsname}" link set veth0 up
	ip -netns "${hsname}" link set lo up

	# configure the VRF on the router which is directly connected to the
	# source host.
	ip -netns "${rtname}" link \
		add "${VRF_DEVNAME}" type vrf table "${VRF_TID}"
	ip -netns "${rtname}" link set "${VRF_DEVNAME}" up

	# enslave the veth interface connecting the router with the host to the
	# VRF in the access router
	ip -netns "${rtname}" link \
		set "${RT2HS_DEVNAME}" master "${VRF_DEVNAME}"

	# set default routes to unreachable for both ipv6 and ipv4
	ip -netns "${rtname}" -6 route \
		add unreachable default metric 4278198272 \
		vrf "${VRF_DEVNAME}"
	ip -netns "${rtname}" -4 route \
		add unreachable default metric 4278198272 \
		vrf "${VRF_DEVNAME}"

	ip -netns "${rtname}" addr \
		add "${IPv6_HS_NETWORK}::254/64" dev "${RT2HS_DEVNAME}" nodad
	ip -netns "${rtname}" addr \
		add "${IPv4_HS_NETWORK}.254/24" dev "${RT2HS_DEVNAME}"

	ip -netns "${rtname}" link set "${RT2HS_DEVNAME}" up

	ip netns exec "${rtname}" \
		sysctl -wq net.ipv6.conf."${RT2HS_DEVNAME}".proxy_ndp=1
	ip netns exec "${rtname}" \
		sysctl -wq net.ipv4.conf."${RT2HS_DEVNAME}".proxy_arp=1

	ip netns exec "${rtname}" sh -c "echo 1 > /proc/sys/net/vrf/strict_mode"
}

setup()
{
	local i

	# create routers
	ROUTERS="1 2 3 4"; readonly ROUTERS
	for i in ${ROUTERS}; do
		create_router "${i}"
	done

	# create hosts
	HOSTS="1 2"; readonly HOSTS
	for i in ${HOSTS}; do
		create_host "${i}"
	done

	# set up the links for connecting routers
	add_link_rt_pairs 1 "2 3 4"
	add_link_rt_pairs 2 "3 4"
	add_link_rt_pairs 3 "4"

	# set up the basic connectivity of routers and routes required for
	# reachability of SIDs.
	setup_rt_networking 1 "2 3 4"
	setup_rt_networking 2 "1 3 4"
	setup_rt_networking 3 "1 2 4"
	setup_rt_networking 4 "1 2 3"

	# set up the hosts connected to routers
	setup_hs 1 1
	setup_hs 2 2

	# set up default SRv6 Endpoints (i.e. SRv6 End and SRv6 End.DT46)
	setup_rt_local_sids 1 "2 3 4"
	setup_rt_local_sids 2 "1 3 4"
	setup_rt_local_sids 3 "1 2 4"
	setup_rt_local_sids 4 "1 2 3"

	# set up SRv6 Policies

	# create an IPv6 VPN between hosts hs-1 and hs-2.
	#
	# Direction hs-1 -> hs-2
	# - rt-1 encap (H.Encaps.Red)
	# - rt-3 SRv6 End.X behavior adj rt-4 (NEXT-C-SID flavor)
	# - rt-4 Plain IPv6 Forwarding to rt-2
	# - rt-2 SRv6 End.DT46 behavior
	setup_ipv6_vpn_1sid 1 2 "3"

	# Direction hs2 -> hs-1
	# - rt-2 encap (H.Encaps.Red)
	# - rt-4 SRv6 End.X behavior adj rt-1 (NEXT-C-SID flavor)
	# - rt-1 SRv6 End.DT46 behavior
	setup_ipv6_vpn_1sid 2 1 "4"

	# create an IPv4 VPN between hosts hs-1 and hs-2
	#
	# Direction hs-1 -> hs-2
	# - rt-1 encap (H.Encaps.Red)
	# - rt-3 SRv6 End.X behavior adj rt-4 (NEXT-C-SID flavor)
	# - rt-4 Plain IPv6 Forwarding to rt-2
	# - rt-2 SRv6 End.DT46 behavior
	setup_ipv4_vpn_2sids 1 2 "3"

	# Direction hs-2 -> hs-1
	# - rt-2 encap (H.Encaps.Red)
	# - rt-3 SRv6 End.X behavior adj rt-4 (NEXT-C-SID flavor)
	# - rt-4 Plain IPv6 Forwarding to rt-1
	# - rt-1 SRv6 End.DT46 behavior
	setup_ipv4_vpn_2sids 2 1 "3"

	# Setup the adjacencies in the SRv6 aware routers
	# - rt-3 SRv6 End.X adjacency with rt-4
	# - rt-4 SRv6 End.X adjacency with rt-1
        set_end_x_nextcsid 3 4
        set_end_x_nextcsid 4 1

	# testing environment was set up successfully
	SETUP_ERR=0
}

check_rt_connectivity()
{
	local rtsrc="$1"
	local rtdst="$2"
	local prefix
	local rtsrc_nsname

	eval rtsrc_nsname=\${$(get_rtname "${rtsrc}")}

	prefix="$(get_network_prefix "${rtsrc}" "${rtdst}")"

	ip netns exec "${rtsrc_nsname}" ping -c 1 -W "${PING_TIMEOUT_SEC}" \
		"${prefix}::${rtdst}" >/dev/null 2>&1
}

check_and_log_rt_connectivity()
{
	local rtsrc="$1"
	local rtdst="$2"

	check_rt_connectivity "${rtsrc}" "${rtdst}"
	log_test $? 0 "Routers connectivity: rt-${rtsrc} -> rt-${rtdst}"
}

check_hs_ipv6_connectivity()
{
	local hssrc="$1"
	local hsdst="$2"
	local hssrc_nsname

	eval hssrc_nsname=\${$(get_hsname "${hssrc}")}

	ip netns exec "${hssrc_nsname}" ping -c 1 -W "${PING_TIMEOUT_SEC}" \
		"${IPv6_HS_NETWORK}::${hsdst}" >/dev/null 2>&1
}

check_hs_ipv4_connectivity()
{
	local hssrc="$1"
	local hsdst="$2"
	local hssrc_nsname

	eval hssrc_nsname=\${$(get_hsname "${hssrc}")}

	ip netns exec "${hssrc_nsname}" ping -c 1 -W "${PING_TIMEOUT_SEC}" \
		"${IPv4_HS_NETWORK}.${hsdst}" >/dev/null 2>&1
}

check_and_log_hs2gw_connectivity()
{
	local hssrc="$1"

	check_hs_ipv6_connectivity "${hssrc}" 254
	log_test $? 0 "IPv6 Hosts connectivity: hs-${hssrc} -> gw"

	check_hs_ipv4_connectivity "${hssrc}" 254
	log_test $? 0 "IPv4 Hosts connectivity: hs-${hssrc} -> gw"
}

check_and_log_hs_ipv6_connectivity()
{
	local hssrc="$1"
	local hsdst="$2"

	check_hs_ipv6_connectivity "${hssrc}" "${hsdst}"
	log_test $? 0 "IPv6 Hosts connectivity: hs-${hssrc} -> hs-${hsdst}"
}

check_and_log_hs_ipv4_connectivity()
{
	local hssrc="$1"
	local hsdst="$2"

	check_hs_ipv4_connectivity "${hssrc}" "${hsdst}"
	log_test $? 0 "IPv4 Hosts connectivity: hs-${hssrc} -> hs-${hsdst}"
}

router_tests()
{
	local i
	local j

	log_section "IPv6 routers connectivity test"

	for i in ${ROUTERS}; do
		for j in ${ROUTERS}; do
			if [ "${i}" -eq "${j}" ]; then
				continue
			fi

			check_and_log_rt_connectivity "${i}" "${j}"
		done
	done
}

host2gateway_tests()
{
	local hs

	log_section "IPv4/IPv6 connectivity test among hosts and gateways"

	for hs in ${HOSTS}; do
		check_and_log_hs2gw_connectivity "${hs}"
	done
}

host_vpn_tests()
{
	log_section "SRv6 VPN connectivity test hosts (h1 <-> h2, IPv6)"

	check_and_log_hs_ipv6_connectivity 1 2
	check_and_log_hs_ipv6_connectivity 2 1

	log_section "SRv6 VPN connectivity test hosts (h1 <-> h2, IPv4)"

	check_and_log_hs_ipv4_connectivity 1 2
	check_and_log_hs_ipv4_connectivity 2 1

	# Setup the adjacencies in the SRv6 aware routers using IPv6 link-local
	# addresses.
	# - rt-3 SRv6 End.X adjacency with rt-4
	# - rt-4 SRv6 End.X adjacency with rt-1
	set_end_x_ll_nextcsid 3 4
	set_end_x_ll_nextcsid 4 1

	log_section "SRv6 VPN connectivity test hosts (h1 <-> h2, IPv6), link-local"

	check_and_log_hs_ipv6_connectivity 1 2
	check_and_log_hs_ipv6_connectivity 2 1

	log_section "SRv6 VPN connectivity test hosts (h1 <-> h2, IPv4), link-local"

	check_and_log_hs_ipv4_connectivity 1 2
	check_and_log_hs_ipv4_connectivity 2 1

	# Restore the previous adjacencies.
	set_end_x_nextcsid 3 4
	set_end_x_nextcsid 4 1
}

__nextcsid_end_x_behavior_test()
{
	local nsname="$1"
	local cmd="$2"
	local blen="$3"
	local flen="$4"
	local layout=""

	if [ "${blen}" != "d" ]; then
		layout="${layout} lblen ${blen}"
	fi

	if [ "${flen}" != "d" ]; then
		layout="${layout} nflen ${flen}"
	fi

	ip -netns "${nsname}" -6 route \
		"${cmd}" "${CSID_CNTR_PREFIX}" \
		table "${CSID_CNTR_RT_TABLE}" \
		encap seg6local action End.X nh6 :: \
		flavors next-csid ${layout} \
		dev "${DUMMY_DEVNAME}" &>/dev/null

	return "$?"
}

rt_x_nextcsid_end_x_behavior_test()
{
	local rt="$1"
	local blen="$2"
	local flen="$3"
	local nsname
	local ret

	eval nsname=\${$(get_rtname "${rt}")}

	__nextcsid_end_x_behavior_test "${nsname}" "add" "${blen}" "${flen}"
	ret="$?"
	__nextcsid_end_x_behavior_test "${nsname}" "del" "${blen}" "${flen}"

	return "${ret}"
}

__parse_csid_container_cfg()
{
	local cfg="$1"
	local index="$2"
	local out

	echo "${cfg}" | cut -d',' -f"${index}"
}

csid_container_cfg_tests()
{
	local valid
	local blen
	local flen
	local cfg
	local ret

	log_section "C-SID Container config tests (legend: d='kernel default')"

	for cfg in "${CSID_CONTAINER_CFGS[@]}"; do
		blen="$(__parse_csid_container_cfg "${cfg}" 1)"
		flen="$(__parse_csid_container_cfg "${cfg}" 2)"
		valid="$(__parse_csid_container_cfg "${cfg}" 3)"

		rt_x_nextcsid_end_x_behavior_test \
			"${CSID_CNTR_RT_ID_TEST}" \
			"${blen}" \
			"${flen}"
		ret="$?"

		if [ "${valid}" == "y" ]; then
			log_test "${ret}" 0 \
				"Accept valid C-SID container cfg (lblen=${blen}, nflen=${flen})"
		else
			log_test "${ret}" 2 \
				"Reject invalid C-SID container cfg (lblen=${blen}, nflen=${flen})"
		fi
	done
}

test_iproute2_supp_or_ksft_skip()
{
	if ! ip route help 2>&1 | grep -qo "next-csid"; then
		echo "SKIP: Missing SRv6 NEXT-C-SID flavor support in iproute2"
		exit "${ksft_skip}"
	fi
}

test_dummy_dev_or_ksft_skip()
{
        local test_netns

        test_netns="dummy-$(mktemp -u XXXXXXXX)"

        if ! ip netns add "${test_netns}"; then
                echo "SKIP: Cannot set up netns for testing dummy dev support"
                exit "${ksft_skip}"
        fi

        modprobe dummy &>/dev/null || true
        if ! ip -netns "${test_netns}" link \
                add "${DUMMY_DEVNAME}" type dummy; then
                echo "SKIP: dummy dev not supported"

                ip netns del "${test_netns}"
                exit "${ksft_skip}"
        fi

        ip netns del "${test_netns}"
}

test_vrf_or_ksft_skip()
{
	modprobe vrf &>/dev/null || true
	if [ ! -e /proc/sys/net/vrf/strict_mode ]; then
		echo "SKIP: vrf sysctl does not exist"
		exit "${ksft_skip}"
	fi
}

if [ "$(id -u)" -ne 0 ]; then
	echo "SKIP: Need root privileges"
	exit "${ksft_skip}"
fi

# required programs to carry out this selftest
test_command_or_ksft_skip ip
test_command_or_ksft_skip ping
test_command_or_ksft_skip sysctl
test_command_or_ksft_skip grep
test_command_or_ksft_skip cut

test_iproute2_supp_or_ksft_skip
test_dummy_dev_or_ksft_skip
test_vrf_or_ksft_skip

set -e
trap cleanup EXIT

setup
set +e

csid_container_cfg_tests

router_tests
host2gateway_tests
host_vpn_tests

print_log_test_results
