#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# author: Andrea Mayer <andrea.mayer@uniroma2.it>
#
# This script is designed for testing the SRv6 H.L2Encaps.Red behavior.
#
# Below is depicted the IPv6 network of an operator which offers L2 VPN
# services to hosts, enabling them to communicate with each other.
# In this example, hosts hs-1 and hs-2 are connected through an L2 VPN service.
# Currently, the SRv6 subsystem in Linux allows hosts hs-1 and hs-2 to exchange
# full L2 frames as long as they carry IPv4/IPv6.
#
# Routers rt-1,rt-2,rt-3 and rt-4 implement L2 VPN services
# leveraging the SRv6 architecture. The key components for such VPNs are:
#
#   i) The SRv6 H.L2Encaps.Red behavior applies SRv6 Policies on traffic
#      received by connected hosts, initiating the VPN tunnel. Such a behavior
#      is an optimization of the SRv6 H.L2Encap aiming to reduce the
#      length of the SID List carried in the pushed SRH. Specifically, the
#      H.L2Encaps.Red removes the first SID contained in the SID List (i.e. SRv6
#      Policy) by storing it into the IPv6 Destination Address. When a SRv6
#      Policy is made of only one SID, the SRv6 H.L2Encaps.Red behavior omits
#      the SRH at all and pushes that SID directly into the IPv6 DA;
#
#  ii) The SRv6 End behavior advances the active SID in the SID List
#      carried by the SRH;
#
# iii) The SRv6 End.DX2 behavior is used for removing the SRv6 Policy
#      and, thus, it terminates the VPN tunnel. The decapsulated L2 frame is
#      sent over the interface connected with the destination host.
#
#               cafe::1                      cafe::2
#              10.0.0.1                     10.0.0.2
#             +--------+                   +--------+
#             |        |                   |        |
#             |  hs-1  |                   |  hs-2  |
#             |        |                   |        |
#             +---+----+                   +--- +---+
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
#
# Every fcf0:0:x:y::/64 network interconnects the SRv6 routers rt-x with rt-y
# in the IPv6 operator network.
#
# Local SID table
# ===============
#
# Each SRv6 router is configured with a Local SID table in which SIDs are
# stored. Considering the given SRv6 router rt-x, at least two SIDs are
# configured in the Local SID table:
#
#   Local SID table for SRv6 router rt-x
#   +----------------------------------------------------------+
#   |fcff:x::e is associated with the SRv6 End behavior        |
#   |fcff:x::d2 is associated with the SRv6 End.DX2 behavior   |
#   +----------------------------------------------------------+
#
# The fcff::/16 prefix is reserved by the operator for implementing SRv6 VPN
# services. Reachability of SIDs is ensured by proper configuration of the IPv6
# operator's network and SRv6 routers.
#
# SRv6 Policies
# =============
#
# An SRv6 ingress router applies SRv6 policies to the traffic received from a
# connected host. SRv6 policy enforcement consists of encapsulating the
# received traffic into a new IPv6 packet with a given SID List contained in
# the SRH.
#
# L2 VPN between hs-1 and hs-2
# ----------------------------
#
# Hosts hs-1 and hs-2 are connected using a dedicated L2 VPN.
# Specifically, packets generated from hs-1 and directed towards hs-2 are
# handled by rt-1 which applies the following SRv6 Policies:
#
#   i.a) L2 traffic, SID List=fcff:2::d2
#
# Policy (i.a) steers tunneled L2 traffic through SRv6 router rt-2.
# The H.L2Encaps.Red omits the presence of SRH at all, since the SID List
# consists of only one SID (fcff:2::d2) that can be stored directly in the IPv6
# DA.
#
# On the reverse path (i.e. from hs-2 to hs-1), rt-2 applies the following
# policies:
#
#   i.b) L2 traffic, SID List=fcff:4::e,fcff:3::e,fcff:1::d2
#
# Policy (i.b) steers tunneled L2 traffic through the SRv6 routers
# rt-4,rt-3,rt2. The H.L2Encaps.Red reduces the SID List in the SRH by removing
# the first SID (fcff:4::e) and pushing it into the IPv6 DA.
#
# In summary:
#  hs-1->hs-2 |IPv6 DA=fcff:2::d2|eth|...|                              (i.a)
#  hs-2->hs-1 |IPv6 DA=fcff:4::e|SRH SIDs=fcff:3::e,fcff:1::d2|eth|...| (i.b)
#

source lib.sh

readonly DUMMY_DEVNAME="dum0"
readonly RT2HS_DEVNAME="veth-hs"
readonly HS_VETH_NAME="veth0"
readonly LOCALSID_TABLE_ID=90
readonly IPv6_RT_NETWORK=fcf0:0
readonly IPv6_HS_NETWORK=cafe
readonly IPv4_HS_NETWORK=10.0.0
readonly VPN_LOCATOR_SERVICE=fcff
readonly MAC_PREFIX=00:00:00:c0:01
readonly END_FUNC=000e
readonly DX2_FUNC=00d2

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

		ip -netns "${nsname}" link set "${devname}" up
	done

	ip -netns "${nsname}" link add "${DUMMY_DEVNAME}" type dummy

	ip -netns "${nsname}" link set "${DUMMY_DEVNAME}" up
	ip -netns "${nsname}" link set lo up

	ip netns exec "${nsname}" sysctl -wq net.ipv6.conf.all.accept_dad=0
	ip netns exec "${nsname}" sysctl -wq net.ipv6.conf.default.accept_dad=0
	ip netns exec "${nsname}" sysctl -wq net.ipv6.conf.all.forwarding=1
	ip netns exec "${nsname}" sysctl -wq net.ipv4.ip_forward=1
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

	eval nsname=\${$(get_rtname "${rt}")}

	for neigh in ${rt_neighs}; do
		devname="veth-rt-${rt}-${neigh}"

		net_prefix="$(get_network_prefix "${rt}" "${neigh}")"

		# set underlay network routes for SIDs reachability
		ip -netns "${nsname}" -6 route \
			add "${VPN_LOCATOR_SERVICE}:${neigh}::/32" \
			table "${LOCALSID_TABLE_ID}" \
			via "${net_prefix}::${neigh}" dev "${devname}"
	done

	# Local End behavior (note that dev "${DUMMY_DEVNAME}" is a dummy
	# interface)
	ip -netns "${nsname}" -6 route \
		add "${VPN_LOCATOR_SERVICE}:${rt}::${END_FUNC}" \
		table "${LOCALSID_TABLE_ID}" \
		encap seg6local action End dev "${DUMMY_DEVNAME}"

	# all SIDs for VPNs start with a common locator. Routes and SRv6
	# Endpoint behaviors instances are grouped together in the 'localsid'
	# table.
	ip -netns "${nsname}" -6 rule add \
		to "${VPN_LOCATOR_SERVICE}::/16" \
		lookup "${LOCALSID_TABLE_ID}" prio 999
}

# build and install the SRv6 policy into the ingress SRv6 router.
# args:
#  $1 - destination host (i.e. cafe::x host)
#  $2 - SRv6 router configured for enforcing the SRv6 Policy
#  $3 - SRv6 routers configured for steering traffic (End behaviors)
#  $4 - SRv6 router configured for removing the SRv6 Policy (router connected
#       to the destination host)
#  $5 - encap mode (full or red)
#  $6 - traffic type (IPv6 or IPv4)
__setup_rt_policy()
{
	local dst="$1"
	local encap_rt="$2"
	local end_rts="$3"
	local dec_rt="$4"
	local mode="$5"
	local traffic="$6"
	local nsname
	local policy=''
	local n

	eval nsname=\${$(get_rtname "${encap_rt}")}

	for n in ${end_rts}; do
		policy="${policy}${VPN_LOCATOR_SERVICE}:${n}::${END_FUNC},"
	done

	policy="${policy}${VPN_LOCATOR_SERVICE}:${dec_rt}::${DX2_FUNC}"

	# add SRv6 policy to incoming traffic sent by connected hosts
	if [ "${traffic}" -eq 6 ]; then
		ip -netns "${nsname}" -6 route \
			add "${IPv6_HS_NETWORK}::${dst}" \
			encap seg6 mode "${mode}" segs "${policy}" \
			dev dum0
	else
		ip -netns "${nsname}" -4 route \
			add "${IPv4_HS_NETWORK}.${dst}" \
			encap seg6 mode "${mode}" segs "${policy}" \
			dev dum0
	fi
}

# see __setup_rt_policy
setup_rt_policy_ipv6()
{
	__setup_rt_policy "$1" "$2" "$3" "$4" "$5" 6
}

#see __setup_rt_policy
setup_rt_policy_ipv4()
{
	__setup_rt_policy "$1" "$2" "$3" "$4" "$5" 4
}

setup_decap()
{
	local rt="$1"
	local nsname

	eval nsname=\${$(get_rtname "${rt}")}

	# Local End.DX2 behavior
	ip -netns "${nsname}" -6 route \
		add "${VPN_LOCATOR_SERVICE}:${rt}::${DX2_FUNC}" \
		table "${LOCALSID_TABLE_ID}" \
		encap seg6local action End.DX2 oif "${RT2HS_DEVNAME}" \
		dev "${RT2HS_DEVNAME}"
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

	ip -netns "${hsname}" link add "${HS_VETH_NAME}" type veth \
		peer name "${RT2HS_DEVNAME}" netns "${rtname}"

	ip -netns "${hsname}" addr add "${IPv6_HS_NETWORK}::${hs}/64" \
		dev "${HS_VETH_NAME}" nodad
	ip -netns "${hsname}" addr add "${IPv4_HS_NETWORK}.${hs}/24" \
		dev "${HS_VETH_NAME}"

	ip -netns "${hsname}" link set "${HS_VETH_NAME}" up
	ip -netns "${hsname}" link set lo up

	ip -netns "${rtname}" addr add "${IPv6_HS_NETWORK}::254/64" \
		dev "${RT2HS_DEVNAME}" nodad
	ip -netns "${rtname}" addr \
		add "${IPv4_HS_NETWORK}.254/24" dev "${RT2HS_DEVNAME}"

	ip -netns "${rtname}" link set "${RT2HS_DEVNAME}" up
}

# set an auto-generated mac address
# args:
#  $1 - name of the node (e.g.: hs-1, rt-3, etc)
#  $2 - id of the node (e.g.: 1 for hs-1, 3 for rt-3, etc)
#  $3 - host part of the IPv6 network address
#  $4 - name of the network interface to which the generated mac address must
#       be set.
set_mac_address()
{
	local nodename="$1"
	local nodeid="$2"
	local host="$3"
	local ifname="$4"
	local nsname

	eval nsname=\${${nodename}}

	ip -netns "${nsname}" link set dev "${ifname}" down

	ip -netns "${nsname}" link set address "${MAC_PREFIX}:${nodeid}" \
		dev "${ifname}"

	# the IPv6 address must be set once again after the MAC address has
	# been changed.
	ip -netns "${nsname}" addr add "${IPv6_HS_NETWORK}::${host}/64" \
		dev "${ifname}" nodad

	ip -netns "${nsname}" link set dev "${ifname}" up
}

set_host_l2peer()
{
	local hssrc="$1"
	local hsdst="$2"
	local ipprefix="$3"
	local proto="$4"
	local hssrc_name
	local ipaddr

	eval hssrc_name=\${$(get_hsname "${hssrc}")}

	if [ "${proto}" -eq 6 ]; then
		ipaddr="${ipprefix}::${hsdst}"
	else
		ipaddr="${ipprefix}.${hsdst}"
	fi

	ip -netns "${hssrc_name}" route add "${ipaddr}" dev "${HS_VETH_NAME}"

	ip -netns "${hssrc_name}" neigh \
		add "${ipaddr}" lladdr "${MAC_PREFIX}:${hsdst}" \
		dev "${HS_VETH_NAME}"
}

# setup an SRv6 L2 VPN between host hs-x and hs-y (currently, the SRv6
# subsystem only supports L2 frames whose layer-3 is IPv4/IPv6).
# args:
#  $1 - source host
#  $2 - SRv6 routers configured for steering tunneled traffic
#  $3 - destination host
setup_l2vpn()
{
	local hssrc="$1"
	local end_rts="$2"
	local hsdst="$3"
	local rtsrc="${hssrc}"
	local rtdst="${hsdst}"

	# set fixed mac for source node and the neigh MAC address
	set_mac_address "hs_${hssrc}" "${hssrc}" "${hssrc}" "${HS_VETH_NAME}"
	set_host_l2peer "${hssrc}" "${hsdst}" "${IPv6_HS_NETWORK}" 6
	set_host_l2peer "${hssrc}" "${hsdst}" "${IPv4_HS_NETWORK}" 4

	# we have to set the mac address of the veth-host (on ingress router)
	# to the mac address of the remote peer (L2 VPN destination host).
	# Otherwise, traffic coming from the source host is dropped at the
	# ingress router.
	set_mac_address "rt_${rtsrc}" "${hsdst}" 254 "${RT2HS_DEVNAME}"

	# set the SRv6 Policies at the ingress router
	setup_rt_policy_ipv6 "${hsdst}" "${rtsrc}" "${end_rts}" "${rtdst}" \
		l2encap.red 6
	setup_rt_policy_ipv4 "${hsdst}" "${rtsrc}" "${end_rts}" "${rtdst}" \
		l2encap.red 4

	# set the decap behavior
	setup_decap "${rtsrc}"
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

	# set up default SRv6 Endpoints (i.e. SRv6 End and SRv6 End.DX2)
	setup_rt_local_sids 1 "2 3 4"
	setup_rt_local_sids 2 "1 3 4"
	setup_rt_local_sids 3 "1 2 4"
	setup_rt_local_sids 4 "1 2 3"

	# create a L2 VPN between hs-1 and hs-2.
	# NB: currently, H.L2Encap* enables tunneling of L2 frames whose
	# layer-3 is IPv4/IPv6.
	#
	# the network path between hs-1 and hs-2 traverses several routers
	# depending on the direction of traffic.
	#
	# Direction hs-1 -> hs-2 (H.L2Encaps.Red)
	# - rt-2 (SRv6 End.DX2 behavior)
	#
	# Direction hs-2 -> hs-1 (H.L2Encaps.Red)
	#  - rt-4,rt-3 (SRv6 End behaviors)
	#  - rt-1 (SRv6 End.DX2 behavior)
	setup_l2vpn 1 "" 2
	setup_l2vpn 2 "4 3" 1

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

check_and_log_hs_connectivity()
{
	local hssrc="$1"
	local hsdst="$2"

	check_and_log_hs_ipv4_connectivity "${hssrc}" "${hsdst}"
	check_and_log_hs_ipv6_connectivity "${hssrc}" "${hsdst}"
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
	log_section "SRv6 L2 VPN connectivity test hosts (h1 <-> h2)"

	check_and_log_hs_connectivity 1 2
	check_and_log_hs_connectivity 2 1
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

test_iproute2_supp_or_ksft_skip()
{
	if ! ip route help 2>&1 | grep -qo "l2encap.red"; then
		echo "SKIP: Missing SRv6 l2encap.red support in iproute2"
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

test_iproute2_supp_or_ksft_skip
test_dummy_dev_or_ksft_skip

set -e
trap cleanup EXIT

setup
set +e

router_tests
host2gateway_tests
host_vpn_tests

print_log_test_results
