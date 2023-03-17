#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# author: Andrea Mayer <andrea.mayer@uniroma2.it>
# author: Paolo Lungaroni <paolo.lungaroni@uniroma2.it>
#
# This script is designed to test the support for "flavors" in the SRv6 End
# behavior.
#
# Flavors defined in RFC8986 [1] represent additional operations that can modify
# or extend the existing SRv6 End, End.X and End.T behaviors. For the sake of
# convenience, we report the list of flavors described in [1] hereafter:
#   - Penultimate Segment Pop (PSP);
#   - Ultimate Segment Pop (USP);
#   - Ultimate Segment Decapsulation (USD).
#
# The End, End.X, and End.T behaviors can support these flavors either
# individually or in combinations.
# Currently in this selftest we consider only the PSP flavor for the SRv6 End
# behavior. However, it is possible to extend the script as soon as other
# flavors will be supported in the kernel.
#
# The purpose of the PSP flavor consists in instructing the penultimate node
# listed in the SRv6 policy to remove (i.e. pop) the outermost SRH from the IPv6
# header.
# A PSP enabled SRv6 End behavior instance processes the SRH by:
#  - decrementing the Segment Left (SL) value from 1 to 0;
#  - copying the last SID from the SID List into the IPv6 Destination Address
#    (DA);
#  - removing the SRH from the extension headers following the IPv6 header.
#
# Once the SRH is removed, the IPv6 packet is forwarded to the destination using
# the IPv6 DA updated during the PSP operation (i.e. the IPv6 DA corresponding
# to the last SID carried by the removed SRH).
#
# Although the PSP flavor can be set for any SRv6 End behavior instance on any
# SR node, it will be active only on such behaviors bound to a penultimate SID
# for a given SRv6 policy.
#                                                SL=2 SL=1 SL=0
#                                                  |    |    |
# For example, given the SRv6 policy (SID List := <X,   Y,   Z>):
#  - a PSP enabled SRv6 End behavior bound to SID Y will apply the PSP operation
#    as Segment Left (SL) is 1, corresponding to the Penultimate Segment of the
#    SID List;
#  - a PSP enabled SRv6 End behavior bound to SID X will *NOT* apply the PSP
#    operation as the Segment Left is 2. This behavior instance will apply the
#    "standard" End packet processing, ignoring the configured PSP flavor at
#    all.
#
# [1] RFC8986: https://datatracker.ietf.org/doc/html/rfc8986
#
# Network topology
# ================
#
# The network topology used in this selftest is depicted hereafter, composed by
# two hosts (hs-1, hs-2) and four routers (rt-1, rt-2, rt-3, rt-4).
# Hosts hs-1 and hs-2 are connected to routers rt-1 and rt-2, respectively,
# allowing them to communicate with each other.
# Traffic exchanged between hs-1 and hs-2 can follow different network paths.
# The network operator, through specific SRv6 Policies can steer traffic to one
# path rather than another. In this selftest this is implemented as follows:
#
#   i) The SRv6 H.Insert behavior applies SRv6 Policies on traffic received by
#      connected hosts. It pushes the Segment Routing Header (SRH) after the
#      IPv6 header. The SRH contains the SID List (i.e. SRv6 Policy) needed for
#      steering traffic across the segments/waypoints specified in that list;
#
#  ii) The SRv6 End behavior advances the active SID in the SID List carried by
#      the SRH;
#
# iii) The PSP enabled SRv6 End behavior is used to remove the SRH when such
#      behavior is configured on a node bound to the Penultimate Segment carried
#      by the SID List.
#
#                cafe::1                      cafe::2
#              +--------+                   +--------+
#              |        |                   |        |
#              |  hs-1  |                   |  hs-2  |
#              |        |                   |        |
#              +---+----+                   +--- +---+
#     cafe::/64    |                             |      cafe::/64
#                  |                             |
#              +---+----+                   +----+---+
#              |        |  fcf0:0:1:2::/64  |        |
#              |  rt-1  +-------------------+  rt-2  |
#              |        |                   |        |
#              +---+----+                   +----+---+
#                  |      .               .      |
#                  |  fcf0:0:1:3::/64   .        |
#                  |          .       .          |
#                  |            .   .            |
#  fcf0:0:1:4::/64 |              .              | fcf0:0:2:3::/64
#                  |            .   .            |
#                  |          .       .          |
#                  |  fcf0:0:2:4::/64   .        |
#                  |      .               .      |
#              +---+----+                   +----+---+
#              |        |                   |        |
#              |  rt-4  +-------------------+  rt-3  |
#              |        |  fcf0:0:3:4::/64  |        |
#              +---+----+                   +----+---+
#
# Every fcf0:0:x:y::/64 network interconnects the SRv6 routers rt-x with rt-y in
# the IPv6 operator network.
#
#
# Local SID table
# ===============
#
# Each SRv6 router is configured with a Local SID table in which SIDs are
# stored. Considering the given SRv6 router rt-x, at least two SIDs are
# configured in the Local SID table:
#
#   Local SID table for SRv6 router rt-x
#   +---------------------------------------------------------------------+
#   |fcff:x::e is associated with the SRv6 End behavior                   |
#   |fcff:x::ef1 is associated with the SRv6 End behavior with PSP flavor |
#   +---------------------------------------------------------------------+
#
# The fcff::/16 prefix is reserved by the operator for the SIDs. Reachability of
# SIDs is ensured by proper configuration of the IPv6 operator's network and
# SRv6 routers.
#
#
# SRv6 Policies
# =============
#
# An SRv6 ingress router applies different SRv6 Policies to the traffic received
# from connected hosts on the basis of the destination addresses.
# In case of SRv6 H.Insert behavior, the SRv6 Policy enforcement consists of
# pushing the SRH (carrying a given SID List) after the existing IPv6 header.
# Note that in the inserting mode, there is no encapsulation at all.
#
#   Before applying an SRv6 Policy using the SRv6 H.Insert behavior
#   +------+---------+
#   | IPv6 | Payload |
#   +------+---------+
#
#   After applying an SRv6 Policy using the SRv6 H.Insert behavior
#   +------+-----+---------+
#   | IPv6 | SRH | Payload |
#   +------+-----+---------+
#
# Traffic from hs-1 to hs-2
# -------------------------
#
# Packets generated from hs-1 and directed towards hs-2 are
# handled by rt-1 which applies the following SRv6 Policy:
#
#   i.a) IPv6 traffic, SID List=fcff:3::e,fcff:4::ef1,fcff:2::ef1,cafe::2
#
# Router rt-1 is configured to enforce the Policy (i.a) through the SRv6
# H.Insert behavior which pushes the SRH after the existing IPv6 header. This
# Policy steers the traffic from hs-1 across rt-3, rt-4, rt-2 and finally to the
# destination hs-2.
#
# As the packet reaches the router rt-3, the SRv6 End behavior bound to SID
# fcff:3::e is triggered. The behavior updates the Segment Left (from SL=3 to
# SL=2) in the SRH, the IPv6 DA with fcff:4::ef1 and forwards the packet to the
# next router on the path, i.e. rt-4.
#
# When router rt-4 receives the packet, the PSP enabled SRv6 End behavior bound
# to SID fcff:4::ef1 is executed. Since the SL=2, the PSP operation is *NOT*
# kicked in and the behavior applies the default End processing: the Segment
# Left is decreased (from SL=2 to SL=1), the IPv6 DA is updated with the SID
# fcff:2::ef1 and the packet is forwarded to router rt-2.
#
# The PSP enabled SRv6 End behavior on rt-2 is associated with SID fcff:2::ef1
# and is executed as the packet is received. Because SL=1, the behavior applies
# the PSP processing on the packet as follows: i) SL is decreased, i.e. from
# SL=1 to SL=0; ii) last SID (cafe::2) is copied into the IPv6 DA; iii) the
# outermost SRH is removed from the extension headers following the IPv6 header.
# Once the PSP processing is completed, the packet is forwarded to the host hs-2
# (destination).
#
# Traffic from hs-2 to hs-1
# -------------------------
#
# Packets generated from hs-2 and directed to hs-1 are handled by rt-2 which
# applies the following SRv6 Policy:
#
#   i.b) IPv6 traffic, SID List=fcff:1::ef1,cafe::1
#
# Router rt-2 is configured to enforce the Policy (i.b) through the SRv6
# H.Insert behavior which pushes the SRH after the existing IPv6 header. This
# Policy steers the traffic from hs-2 across rt-1 and finally to the
# destination hs-1
#
#
# When the router rt-1 receives the packet, the PSP enabled SRv6 End behavior
# associated with the SID fcff:1::ef1 is triggered. Since the SL=1,
# the PSP operation takes place: i) the SL is decremented; ii) the IPv6 DA is
# set with the last SID; iii) the SRH is removed from the extension headers
# after the IPv6 header. At this point, the packet with IPv6 DA=cafe::1 is sent
# to the destination, i.e. hs-1.

# Kselftest framework requirement - SKIP code is 4.
readonly ksft_skip=4

readonly RDMSUFF="$(mktemp -u XXXXXXXX)"
readonly DUMMY_DEVNAME="dum0"
readonly RT2HS_DEVNAME="veth1"
readonly LOCALSID_TABLE_ID=90
readonly IPv6_RT_NETWORK=fcf0:0
readonly IPv6_HS_NETWORK=cafe
readonly IPv6_TESTS_ADDR=2001:db8::1
readonly LOCATOR_SERVICE=fcff
readonly END_FUNC=000e
readonly END_PSP_FUNC=0ef1

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

get_nodename()
{
	local name="$1"

	echo "${name}-${RDMSUFF}"
}

get_rtname()
{
	local rtid="$1"

	get_nodename "rt-${rtid}"
}

get_hsname()
{
	local hsid="$1"

	get_nodename "hs-${hsid}"
}

__create_namespace()
{
	local name="$1"

	ip netns add "${name}"
}

create_router()
{
	local rtid="$1"
	local nsname

	nsname="$(get_rtname "${rtid}")"

	__create_namespace "${nsname}"
}

create_host()
{
	local hsid="$1"
	local nsname

	nsname="$(get_hsname "${hsid}")"

	__create_namespace "${nsname}"
}

cleanup()
{
	local nsname
	local i

	# destroy routers
	for i in ${ROUTERS}; do
		nsname="$(get_rtname "${i}")"

		ip netns del "${nsname}" &>/dev/null || true
	done

	# destroy hosts
	for i in ${HOSTS}; do
		nsname="$(get_hsname "${i}")"

		ip netns del "${nsname}" &>/dev/null || true
	done

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

	nsname="$(get_rtname "${rt}")"

	for neigh in ${rt_neighs}; do
		neigh_nsname="$(get_rtname "${neigh}")"

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

# Given the description of a router <id:op> as an input, the function returns
# the <id> token which represents the ID of the router.
# i.e. input: "12:psp"
#      output: "12"
__get_srv6_rtcfg_id()
{
	local element="$1"

	echo "${element}" | cut -d':' -f1
}

# Given the description of a router <id:op> as an input, the function returns
# the <op> token which represents the operation (e.g. End behavior with or
# withouth flavors) configured for the node.

# Note that when the operation represents an End behavior with a list of
# flavors, the output is the ordered version of that list.
# i.e. input: "5:usp,psp,usd"
#      output: "psp,usd,usp"
__get_srv6_rtcfg_op()
{
	local element="$1"

	# return the lexicographically ordered flavors
	echo "${element}" | cut -d':' -f2 | sed 's/,/\n/g' | sort | \
		xargs | sed 's/ /,/g'
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

	nsname="$(get_rtname "${rt}")"

	for neigh in ${rt_neighs}; do
		devname="veth-rt-${rt}-${neigh}"

		net_prefix="$(get_network_prefix "${rt}" "${neigh}")"

		ip -netns "${nsname}" addr \
			add "${net_prefix}::${rt}/64" dev "${devname}" nodad

		ip -netns "${nsname}" link set "${devname}" up
	done

	ip -netns "${nsname}" link set lo up

	ip -netns "${nsname}" link add ${DUMMY_DEVNAME} type dummy
	ip -netns "${nsname}" link set ${DUMMY_DEVNAME} up

	ip netns exec "${nsname}" sysctl -wq net.ipv6.conf.all.accept_dad=0
	ip netns exec "${nsname}" sysctl -wq net.ipv6.conf.default.accept_dad=0
	ip netns exec "${nsname}" sysctl -wq net.ipv6.conf.all.forwarding=1
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

	nsname="$(get_rtname "${rt}")"

	for neigh in ${rt_neighs}; do
		devname="veth-rt-${rt}-${neigh}"

		net_prefix="$(get_network_prefix "${rt}" "${neigh}")"

		# set underlay network routes for SIDs reachability
		ip -netns "${nsname}" -6 route \
			add "${LOCATOR_SERVICE}:${neigh}::/32" \
			table "${LOCALSID_TABLE_ID}" \
			via "${net_prefix}::${neigh}" dev "${devname}"
	done

	# Local End behavior (note that "dev" is a dummy interface chosen for
	# the sake of simplicity).
	ip -netns "${nsname}" -6 route \
		add "${LOCATOR_SERVICE}:${rt}::${END_FUNC}" \
		table "${LOCALSID_TABLE_ID}" \
		encap seg6local action End dev "${DUMMY_DEVNAME}"


	# all SIDs start with a common locator. Routes and SRv6 Endpoint
	# behavior instaces are grouped together in the 'localsid' table.
	ip -netns "${nsname}" -6 rule \
		add to "${LOCATOR_SERVICE}::/16" \
		lookup "${LOCALSID_TABLE_ID}" prio 999

	# set default routes to unreachable
	ip -netns "${nsname}" -6 route \
		add unreachable default metric 4278198272 \
		dev "${DUMMY_DEVNAME}"
}

# This helper function builds and installs the SID List (i.e. SRv6 Policy)
# to be applied on incoming packets at the ingress node. Moreover, it
# configures the SRv6 nodes specified in the SID List to process the traffic
# according to the operations required by the Policy itself.
# args:
#  $1 - destination host (i.e. cafe::x host)
#  $2 - SRv6 router configured for enforcing the SRv6 Policy
#  $3 - compact way to represent a list of SRv6 routers with their operations
#       (i.e. behaviors) that each of them needs to perform. Every <nodeid:op>
#       element constructs a SID that is associated with the behavior <op> on
#       the <nodeid> node. The list of such elements forms an SRv6 Policy.
__setup_rt_policy()
{
	local dst="$1"
	local encap_rt="$2"
	local policy_rts="$3"
	local behavior_cfg
	local in_nsname
	local rt_nsname
	local policy=''
	local function
	local fullsid
	local op_type
	local node
	local n

	in_nsname="$(get_rtname "${encap_rt}")"

	for n in ${policy_rts}; do
		node="$(__get_srv6_rtcfg_id "${n}")"
		op_type="$(__get_srv6_rtcfg_op "${n}")"
		rt_nsname="$(get_rtname "${node}")"

		case "${op_type}" in
		"noflv")
			policy="${policy}${LOCATOR_SERVICE}:${node}::${END_FUNC},"
			function="${END_FUNC}"
			behavior_cfg="End"
			;;

		"psp")
			policy="${policy}${LOCATOR_SERVICE}:${node}::${END_PSP_FUNC},"
			function="${END_PSP_FUNC}"
			behavior_cfg="End flavors psp"
			;;

		*)
			break
			;;
		esac

		fullsid="${LOCATOR_SERVICE}:${node}::${function}"

		# add SRv6 Endpoint behavior to the selected router
		if ! ip -netns "${rt_nsname}" -6 route get "${fullsid}" \
			&>/dev/null; then
			ip -netns "${rt_nsname}" -6 route \
				add "${fullsid}" \
				table "${LOCALSID_TABLE_ID}" \
				encap seg6local action ${behavior_cfg} \
				dev "${DUMMY_DEVNAME}"
		fi
	done

	# we need to remove the trailing comma to avoid inserting an empty
	# address (::0) in the SID List.
	policy="${policy%,}"

	# add SRv6 policy to incoming traffic sent by connected hosts
	ip -netns "${in_nsname}" -6 route \
		add "${IPv6_HS_NETWORK}::${dst}" \
		encap seg6 mode inline segs "${policy}" \
		dev "${DUMMY_DEVNAME}"

	ip -netns "${in_nsname}" -6 neigh \
		add proxy "${IPv6_HS_NETWORK}::${dst}" \
		dev "${RT2HS_DEVNAME}"
}

# see __setup_rt_policy
setup_rt_policy_ipv6()
{
	__setup_rt_policy "$1" "$2" "$3"
}

setup_hs()
{
	local hs="$1"
	local rt="$2"
	local hsname
	local rtname

	hsname="$(get_hsname "${hs}")"
	rtname="$(get_rtname "${rt}")"

	ip netns exec "${hsname}" sysctl -wq net.ipv6.conf.all.accept_dad=0
	ip netns exec "${hsname}" sysctl -wq net.ipv6.conf.default.accept_dad=0

	ip -netns "${hsname}" link add veth0 type veth \
		peer name "${RT2HS_DEVNAME}" netns "${rtname}"

	ip -netns "${hsname}" addr \
		add "${IPv6_HS_NETWORK}::${hs}/64" dev veth0 nodad

	ip -netns "${hsname}" link set veth0 up
	ip -netns "${hsname}" link set lo up

	ip -netns "${rtname}" addr \
		add "${IPv6_HS_NETWORK}::254/64" dev "${RT2HS_DEVNAME}" nodad

	ip -netns "${rtname}" link set "${RT2HS_DEVNAME}" up

	ip netns exec "${rtname}" \
		sysctl -wq net.ipv6.conf."${RT2HS_DEVNAME}".proxy_ndp=1
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

	# set up default SRv6 Endpoints (i.e. SRv6 End behavior)
	setup_rt_local_sids 1 "2 3 4"
	setup_rt_local_sids 2 "1 3 4"
	setup_rt_local_sids 3 "1 2 4"
	setup_rt_local_sids 4 "1 2 3"

	# set up SRv6 policies
	# create a connection between hosts hs-1 and hs-2.
	# The path between hs-1 and hs-2 traverses SRv6 aware routers.
	# For each direction two path are chosen:
	#
	# Direction hs-1 -> hs-2 (PSP flavor)
	#  - rt-1 (SRv6 H.Insert policy)
	#  - rt-3 (SRv6 End behavior)
	#  - rt-4 (SRv6 End flavor PSP with SL>1, acting as End behavior)
	#  - rt-2 (SRv6 End flavor PSP with SL=1)
	#
	# Direction hs-2 -> hs-1 (PSP flavor)
	#  - rt-2 (SRv6 H.Insert policy)
	#  - rt-1 (SRv6 End flavor PSP with SL=1)
	setup_rt_policy_ipv6 2 1 "3:noflv 4:psp 2:psp"
	setup_rt_policy_ipv6 1 2 "1:psp"

	# testing environment was set up successfully
	SETUP_ERR=0
}

check_rt_connectivity()
{
	local rtsrc="$1"
	local rtdst="$2"
	local prefix
	local rtsrc_nsname

	rtsrc_nsname="$(get_rtname "${rtsrc}")"

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

	hssrc_nsname="$(get_hsname "${hssrc}")"

	ip netns exec "${hssrc_nsname}" ping -c 1 -W "${PING_TIMEOUT_SEC}" \
		"${IPv6_HS_NETWORK}::${hsdst}" >/dev/null 2>&1
}

check_and_log_hs2gw_connectivity()
{
	local hssrc="$1"

	check_hs_ipv6_connectivity "${hssrc}" 254
	log_test $? 0 "IPv6 Hosts connectivity: hs-${hssrc} -> gw"
}

check_and_log_hs_ipv6_connectivity()
{
	local hssrc="$1"
	local hsdst="$2"

	check_hs_ipv6_connectivity "${hssrc}" "${hsdst}"
	log_test $? 0 "IPv6 Hosts connectivity: hs-${hssrc} -> hs-${hsdst}"
}

check_and_log_hs_connectivity()
{
	local hssrc="$1"
	local hsdst="$2"

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

	log_section "IPv6 connectivity test among hosts and gateways"

	for hs in ${HOSTS}; do
		check_and_log_hs2gw_connectivity "${hs}"
	done
}

host_srv6_end_flv_psp_tests()
{
	log_section "SRv6 connectivity test hosts (h1 <-> h2, PSP flavor)"

	check_and_log_hs_connectivity 1 2
	check_and_log_hs_connectivity 2 1
}

test_iproute2_supp_or_ksft_skip()
{
	local flavor="$1"

	if ! ip route help 2>&1 | grep -qo "${flavor}"; then
		echo "SKIP: Missing SRv6 ${flavor} flavor support in iproute2"
		exit "${ksft_skip}"
	fi
}

test_kernel_supp_or_ksft_skip()
{
	local flavor="$1"
	local test_netns

	test_netns="kflv-$(mktemp -u XXXXXXXX)"

	if ! ip netns add "${test_netns}"; then
		echo "SKIP: Cannot set up netns to test kernel support for flavors"
		exit "${ksft_skip}"
	fi

	if ! ip -netns "${test_netns}" link \
		add "${DUMMY_DEVNAME}" type dummy; then
		echo "SKIP: Cannot set up dummy dev to test kernel support for flavors"

		ip netns del "${test_netns}"
		exit "${ksft_skip}"
	fi

	if ! ip -netns "${test_netns}" link \
		set "${DUMMY_DEVNAME}" up; then
		echo "SKIP: Cannot activate dummy dev to test kernel support for flavors"

		ip netns del "${test_netns}"
		exit "${ksft_skip}"
	fi

	if ! ip -netns "${test_netns}" -6 route \
		add "${IPv6_TESTS_ADDR}" encap seg6local \
		action End flavors "${flavor}" dev "${DUMMY_DEVNAME}"; then
		echo "SKIP: ${flavor} flavor not supported in kernel"

		ip netns del "${test_netns}"
		exit "${ksft_skip}"
	fi

	ip netns del "${test_netns}"
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
test_command_or_ksft_skip sed
test_command_or_ksft_skip sort
test_command_or_ksft_skip xargs

test_dummy_dev_or_ksft_skip
test_iproute2_supp_or_ksft_skip psp
test_kernel_supp_or_ksft_skip psp

set -e
trap cleanup EXIT

setup
set +e

router_tests
host2gateway_tests
host_srv6_end_flv_psp_tests

print_log_test_results
