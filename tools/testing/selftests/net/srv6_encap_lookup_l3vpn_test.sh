#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# author: Andrea Mayer <andrea.mayer@uniroma2.it>

# This test evaluates the SRv6 encap "lookup" attribute. After encapsulation
# the router looks up the route for the first SID, that is the outer IPv6
# destination of the encapsulated packet. The attribute selects the FIB table
# used for this post-encap SID route lookup.
#
# Two routers (rt-1, rt-2) provide L3 VPN services over an IPv6 underlay
# (fd00::/64). Each router uses a separate VRF per tenant, with default
# blackhole routes (IPv4 and IPv6) to prevent traffic from leaking out of
# the VRF. Tenant traffic is encapsulated, then decapsulated with an
# End.DT46. Each router proxies both NDP and ARP.
#
# The routes that match the first SIDs are installed in a dedicated underlay
# table (500) rather than the main table (254). The encap routes use
# "lookup 500" to select this table for the post-encap SID route lookup.
#
# Without the "lookup" attribute, the route for the first SID cannot be found:
#  - on the input path (forwarded traffic), the lookup stays in the VRF
#    and hits the blackhole;
#  - on the output path (locally originated traffic), the lookup falls
#    through to the main table, with no route to the first SID.
#
#
# Legend (specific per-tenant addresses are in the instantiation tables below):
#   X          = tenant id and VRF table id; two tenants: 100 and 200
#                ("tX" means tenant X, e.g. t100, t200)
#   a, b       = the two host ids of the tenant
#   HA, HB     = addresses of host a, host b
#   RLO1, RLO2 = rlo-X router loopback address on rt-1, rt-2 (tenant gateway
#                for the output path, dual-stack)
#   vrf-X      = per-tenant VRF on each router (table X)
#
# Constants (same for every tenant):
#   underlay   = table 500; post-encap SID route lookup (via "lookup 500")
#   localsid   = table 90; holds the decap SIDs (End.DT46)
#   fd00::/64  = underlay link between rt-1 and rt-2
#   veth-tX    = cafe::254/10.0.0.254 (tenant gateway on veth, both routers)
#
#
# +-------------------+                                   +-------------------+
# |                   |                                   |                   |
# |   hs-tX-a netns   |                                   |   hs-tX-b netns   |
# |                   |                                   |                   |
# |  +-------------+  |                                   |  +-------------+  |
# |  |    veth0    |  |                                   |  |    veth0    |  |
# |  |     HA      |  |                                   |  |     HB      |  |
# |  +-------------+  |                                   |  +-------------+  |
# |        .          |                                   |         .         |
# +-------------------+                                   +-------------------+
#          .                                                        .
#          .                                                        .
# +-----------------------------------+   +-----------------------------------+
# |        .                          |   |                         .         |
# | +---------------+                 |   |                 +---------------+ |
# | |   veth-tX     |   +----------+  |   |  +----------+   |   veth-tX     | |
# | |  ::254/.254   |   | localsid |  |   |  | localsid |   |  ::254/.254   | |
# | +-------+-------+   +----------+  |   |  +----------+   +-------+-------+ |
# |         |           +----------+  |   |  +----------+           |         |
# |    +----+----+      | underlay |  |   |  | underlay |      +----+----+    |
# |    | vrf-X   |      +----------+  |   |  +----------+      | vrf-X   |    |
# |    +----+----+                    |   |                    +----+----+    |
# |         |                         |   |                         |         |
# |   +-----+----+    +------------+  |   |  +------------+    +----+-----+   |
# |   |  rlo-X   |    |   veth0    |  |   |  |   veth0    |    |  rlo-X   |   |
# |   |   RLO1   |    | fd00::1/64 |..|...|..| fd00::2/64 |    |   RLO2   |   |
# |   +----------+    +------------+  |   |  +------------+    +----------+   |
# |              rt-1 netns           |   |          rt-2 netns               |
# +-----------------------------------+   +-----------------------------------+
#
#
# Per-tenant instantiation:
# +-----+------+-------------------+-------------------+
# |  X  | a, b | HA                | HB                |
# +-----+------+-------------------+-------------------+
# | 100 | 1, 2 | cafe::1, 10.0.0.1 | cafe::2, 10.0.0.2 |
# | 200 | 3, 4 | cafe::3, 10.0.0.3 | cafe::4, 10.0.0.4 |
# +-----+------+-------------------+-------------------+
#
# Router loopback (rlo-X) addresses, per tenant:
# +-----+-----------------------+-----------------------+
# |  X  | RLO1 (rt-1)           | RLO2 (rt-2)           |
# +-----+-----------------------+-----------------------+
# | 100 | cafe::101, 10.0.0.101 | cafe::102, 10.0.0.102 |
# | 200 | cafe::201, 10.0.0.201 | cafe::202, 10.0.0.202 |
# +-----+-----------------------+-----------------------+
#
#
# Network configuration
# =====================
#
# rt-1: localsid table (table 90)
# +--------+--------------------+----------------------------------+
# | tenant | SID                | Action                           |
# +--------+--------------------+----------------------------------+
# |  100   | fc00:2:1:100::0d46 | apply SRv6 End.DT46 vrftable 100 |
# |  200   | fc00:2:1:200::0d46 | apply SRv6 End.DT46 vrftable 200 |
# +--------+--------------------+----------------------------------+
#
# rt-1: underlay table (table 500) - post-encap SID route lookup
# +--------+--------------------+-------------------------------+
# | tenant | SID                | Action                        |
# +--------+--------------------+-------------------------------+
# |  100   | fc00:1:2:100::0d46 | forward via fd00::2 dev veth0 |
# |  200   | fc00:1:2:200::0d46 | forward via fd00::2 dev veth0 |
# +--------+--------------------+-------------------------------+
#
# rt-1: VRF tables (per tenant: vrf-X = table X)
# +--------+------------+------------------------------------------+
# | tenant | dst        | encap action                             |
# +--------+------------+------------------------------------------+
# | 100    | cafe::2    | encap segs fc00:1:2:100::0d46 lookup 500 |
# |        | 10.0.0.2   |                                          |
# |        | cafe::102  |                                          |
# |        | 10.0.0.102 |                                          |
# | 200    | cafe::4    | encap segs fc00:1:2:200::0d46 lookup 500 |
# |        | 10.0.0.4   |                                          |
# |        | cafe::202  |                                          |
# |        | 10.0.0.202 |                                          |
# +--------+------------+------------------------------------------+
#
#
# rt-2: localsid table (table 90)
# +--------+--------------------+----------------------------------+
# | tenant | SID                | Action                           |
# +--------+--------------------+----------------------------------+
# |  100   | fc00:1:2:100::0d46 | apply SRv6 End.DT46 vrftable 100 |
# |  200   | fc00:1:2:200::0d46 | apply SRv6 End.DT46 vrftable 200 |
# +--------+--------------------+----------------------------------+
#
# rt-2: underlay table (table 500) - post-encap SID route lookup
# +--------+--------------------+-------------------------------+
# | tenant | SID                | Action                        |
# +--------+--------------------+-------------------------------+
# |  100   | fc00:2:1:100::0d46 | forward via fd00::1 dev veth0 |
# |  200   | fc00:2:1:200::0d46 | forward via fd00::1 dev veth0 |
# +--------+--------------------+-------------------------------+
#
# rt-2: VRF tables (per tenant: vrf-X = table X)
# +--------+------------+------------------------------------------+
# | tenant | dst        | encap action                             |
# +--------+------------+------------------------------------------+
# | 100    | cafe::1    | encap segs fc00:2:1:100::0d46 lookup 500 |
# |        | 10.0.0.1   |                                          |
# |        | cafe::101  |                                          |
# |        | 10.0.0.101 |                                          |
# | 200    | cafe::3    | encap segs fc00:2:1:200::0d46 lookup 500 |
# |        | 10.0.0.3   |                                          |
# |        | cafe::201  |                                          |
# |        | 10.0.0.201 |                                          |
# +--------+------------+------------------------------------------+
# Within a tenant, a single SID reaches the adjacent router (its loopback)
# and the remote host connected to it, in both IPv4 and IPv6.
#
# For both rt-1 and rt-2, each VRF also has the connected host prefix (cafe::/64
# or 10.0.0.0/24) and a default blackhole (IPv4 and IPv6).
#
#
# Locally originated traffic (output path)
# ========================================
#
# The configuration above covers forwarded traffic, where packets arrive from
# a host and are encapsulated by the router. To also test router-originated
# traffic, each router pings the other router's loopback address through
# the VPN.
#
# Example (tenant 100), rt-1 pings cafe::102 (rt-2's loopback):
#   1. rt-1 looks up cafe::102 in vrf-100 and encapsulates it (SID
#      fc00:1:2:100::0d46), then "lookup 500" finds the route for the SID in the
#      underlay table (next hop fd00::2) and forwards it;
#   2. rt-2 decapsulates it (localsid, End.DT46) and delivers it locally
#      (cafe::102 is on the rlo-100 interface);
#   3. rt-2 replies with destination cafe::101 (rt-1's loopback). rt-2 looks up
#      cafe::101 in vrf-100 and encapsulates it back to rt-1 (again via "lookup
#      500"). rt-1 decapsulates it and delivers it.

# shellcheck source=lib.sh
source lib.sh

readonly LOCALSID_TABLE_ID=90
readonly UNDERLAY_TABLE_ID=500
readonly IPv6_RT_NETWORK=fd00
readonly IPv6_HS_NETWORK=cafe
readonly IPv4_HS_NETWORK=10.0.0
readonly VPN_LOCATOR_SERVICE=fc00
readonly DT46_FUNC=0d46
readonly DUMMY_DEVNAME=dum0
readonly IPv6_TESTS_ADDR=2001:db8::1
readonly TESTS_TABLE_ID=54321
PING_TIMEOUT_SEC=4

SETUP_ERR=1

ret=${ksft_skip}
nsuccess=0
nfail=0

PAUSE_ON_FAIL=${PAUSE_ON_FAIL:=no}

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
			read -r a
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

get_rtname()
{
	local rtid="$1"

	echo "rt_${rtid}"
}

get_rt_nsname()
{
	local rtid="$1"
	local varname

	varname="$(get_rtname "${rtid}")"
	echo "${!varname}"
}

get_hsname()
{
	local tid="$1"
	local hsid="$2"

	echo "hs_t${tid}_${hsid}"
}

get_hs_nsname()
{
	local tid="$1"
	local hsid="$2"
	local varname

	varname="$(get_hsname "${tid}" "${hsid}")"
	echo "${!varname}"
}

cleanup()
{
	ip link del veth-rt-1 2>/dev/null || true
	ip link del veth-rt-2 2>/dev/null || true

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

# Host id of the router loopback (rlo) for a (router, tenant) pair.
# E.g. rt-1/tenant 100 -> 101, rt-2/tenant 200 -> 202.
get_rlo_hostid()
{
	local rtid="$1"
	local tid="$2"

	echo "$((tid + rtid))"
}

build_vpn_sid()
{
	local rtsrc="$1"
	local rtdst="$2"
	local tid="$3"

	echo "${VPN_LOCATOR_SERVICE}:${rtsrc}:${rtdst}:${tid}::${DT46_FUNC}"
}

# Install a dual-stack (IPv6 and IPv4) encap route in a VRF on the given
# router.
# args:
#  $1 - router id
#  $2 - host part of the IPv6 destination
#  $3 - host part of the IPv4 destination
#  $4 - SRv6 SID used as the encap destination
#  $5 - tenant id
#  $6 - if "true", add the "lookup" attribute to the encap route
__set_encap_route()
{
	local rt="$1"
	local dst6="$2"
	local dst4="$3"
	local sid="$4"
	local tid="$5"
	local use_lookup="$6"
	local lookup=''
	local rtname

	rtname="$(get_rt_nsname "${rt}")"

	if [ "${use_lookup}" = "true" ]; then
		lookup="lookup ${UNDERLAY_TABLE_ID}"
	fi

	# shellcheck disable=SC2086
	ip -netns "${rtname}" -6 route replace \
		"${IPv6_HS_NETWORK}::${dst6}/128" vrf "vrf-${tid}" \
		encap seg6 mode encap segs "${sid}" ${lookup} dev veth0

	# shellcheck disable=SC2086
	ip -netns "${rtname}" -4 route replace \
		"${IPv4_HS_NETWORK}.${dst4}/32" vrf "vrf-${tid}" \
		encap seg6 mode encap segs "${sid}" ${lookup} dev veth0
}

# Install the dual-stack encap route for a tenant host on rt, with the
# "lookup" attribute so the first SID is looked up in the underlay table.
# args:
#  $1 - router id where the encap route is installed
#  $2 - host destination id (host part of cafe::<id>/128 and 10.0.0.<id>/32)
#  $3 - SRv6 SID used as the encap destination
#  $4 - tenant id
set_host_encap_route()
{
	local rt="$1"
	local hsdst="$2"
	local sid="$3"
	local tid="$4"

	__set_encap_route "${rt}" "${hsdst}" "${hsdst}" "${sid}" "${tid}" true
}

set_host_encap_route_nolookup()
{
	local rt="$1"
	local hsdst="$2"
	local sid="$3"
	local tid="$4"

	__set_encap_route "${rt}" "${hsdst}" "${hsdst}" "${sid}" "${tid}" false
}

# Install the dual-stack encap route on rtsrc toward rtdst's rlo loopback
# (RLO1 or RLO2, see header), with the "lookup" attribute so the first
# SID is looked up in the underlay table.
# args:
#  $1 - router id where the encap route is installed
#  $2 - router id whose loopback address is the route destination
#  $3 - SRv6 SID used as the encap destination
#  $4 - tenant id
set_gw_encap_route()
{
	local rtsrc="$1"
	local rtdst="$2"
	local sid="$3"
	local tid="$4"
	local dst

	dst="$(get_rlo_hostid "${rtdst}" "${tid}")"

	__set_encap_route "${rtsrc}" "${dst}" "${dst}" "${sid}" "${tid}" true
}

set_gw_encap_route_nolookup()
{
	local rtsrc="$1"
	local rtdst="$2"
	local sid="$3"
	local tid="$4"
	local dst

	dst="$(get_rlo_hostid "${rtdst}" "${tid}")"

	__set_encap_route "${rtsrc}" "${dst}" "${dst}" "${sid}" "${tid}" false
}

# Setup the basic networking for a router
setup_rt_networking()
{
	local id="$1"
	local nsname

	nsname="$(get_rt_nsname "${id}")"

	ip link set "veth-rt-${id}" netns "${nsname}"
	ip -netns "${nsname}" link set "veth-rt-${id}" name veth0

	ip netns exec "${nsname}" sysctl -wq net.ipv6.conf.all.accept_dad=0
	ip netns exec "${nsname}" sysctl -wq net.ipv6.conf.default.accept_dad=0

	ip -netns "${nsname}" addr add "${IPv6_RT_NETWORK}::${id}/64" dev veth0 nodad
	ip -netns "${nsname}" link set veth0 up

	ip netns exec "${nsname}" sysctl -wq net.ipv4.ip_forward=1
	ip netns exec "${nsname}" sysctl -wq net.ipv6.conf.all.forwarding=1
}

# Setup a host namespace and attach it to its gateway
setup_hs()
{
	local hid="$1"
	local rid="$2"
	local tid="$3"
	local rtveth="veth-t${tid}"
	local hsname
	local rtname

	hsname="$(get_hs_nsname "${tid}" "${hid}")"
	rtname="$(get_rt_nsname "${rid}")"

	ip netns exec "${hsname}" sysctl -wq net.ipv6.conf.all.accept_dad=0
	ip netns exec "${hsname}" sysctl -wq net.ipv6.conf.default.accept_dad=0

	ip -netns "${hsname}" link add veth0 type veth peer name "${rtveth}"
	ip -netns "${hsname}" link set "${rtveth}" netns "${rtname}"

	ip -netns "${hsname}" addr add \
		"${IPv6_HS_NETWORK}::${hid}/64" dev veth0 nodad
	ip -netns "${hsname}" addr add \
		"${IPv4_HS_NETWORK}.${hid}/24" dev veth0

	ip -netns "${hsname}" link set veth0 up
}

# Setup the per-tenant VRF on a router (gateway, loopback, blackhole)
setup_rt()
{
	local rid="$1"
	local tid="$2"
	local rtveth="veth-t${tid}"
	local rlo_dev="rlo-${tid}"
	local rtname
	local gw_addr_v6
	local gw_addr_v4

	rtname="$(get_rt_nsname "${rid}")"

	gw_addr_v6="${IPv6_HS_NETWORK}::$(get_rlo_hostid "${rid}" "${tid}")"
	gw_addr_v4="${IPv4_HS_NETWORK}.$(get_rlo_hostid "${rid}" "${tid}")"

	ip -netns "${rtname}" link add "vrf-${tid}" type vrf table "${tid}"
	ip -netns "${rtname}" link set "vrf-${tid}" up

	ip -netns "${rtname}" link set "${rtveth}" master "vrf-${tid}"

	ip -netns "${rtname}" addr add \
		"${IPv6_HS_NETWORK}::254/64" dev "${rtveth}" nodad
	ip -netns "${rtname}" addr add \
		"${IPv4_HS_NETWORK}.254/24" dev "${rtveth}"

	ip -netns "${rtname}" link set "${rtveth}" up

	ip netns exec "${rtname}" \
		sysctl -wq "net.ipv6.conf.${rtveth}.proxy_ndp=1"
	ip netns exec "${rtname}" \
		sysctl -wq "net.ipv4.conf.${rtveth}.proxy_arp=1"

	ip netns exec "${rtname}" sh -c "echo 1 > /proc/sys/net/vrf/strict_mode"

	# router loopback interface for locally originated traffic
	ip -netns "${rtname}" link add "${rlo_dev}" type dummy
	ip -netns "${rtname}" link set "${rlo_dev}" master "vrf-${tid}"

	ip -netns "${rtname}" addr add "${gw_addr_v6}/128" \
		dev "${rlo_dev}" nodad
	ip -netns "${rtname}" addr add "${gw_addr_v4}/32" \
		dev "${rlo_dev}"

	ip -netns "${rtname}" link set "${rlo_dev}" up

	# default blackhole routes in the VRF: any traffic that does not match
	# a specific route is dropped. Without the "lookup" attribute on the
	# encap route, the route for the first SID cannot be found from within
	# the VRF.
	ip -netns "${rtname}" -6 route add blackhole default metric 4278198272 \
		vrf "vrf-${tid}"
	ip -netns "${rtname}" -4 route add blackhole default metric 4278198272 \
		vrf "vrf-${tid}"
}

# Configure a one-way VPN path towards hsdst (on rtdst) for tenant tid.
# The encap side is set up on rtsrc and the decap side on rtdst.
# args:
#  $1 - router id where the encap side is set up
#  $2 - host id of the destination host
#  $3 - router id of the destination router (connected to the destination host)
#  $4 - tenant id
setup_vpn_config()
{
	local rtsrc="$1"
	local hsdst="$2"
	local rtdst="$3"
	local tid="$4"
	local rtveth="veth-t${tid}"
	local rtsrc_name
	local rtdst_name
	local vpn_sid

	rtsrc_name="$(get_rt_nsname "${rtsrc}")"
	rtdst_name="$(get_rt_nsname "${rtdst}")"
	vpn_sid="$(build_vpn_sid "${rtsrc}" "${rtdst}" "${tid}")"

	ip -netns "${rtsrc_name}" -6 neigh add proxy \
		"${IPv6_HS_NETWORK}::${hsdst}" dev "${rtveth}"
	set_host_encap_route "${rtsrc}" "${hsdst}" "${vpn_sid}" "${tid}"

	ip -netns "${rtsrc_name}" -6 route add "${vpn_sid}/128" \
		table "${UNDERLAY_TABLE_ID}" \
		via "fd00::${rtdst}" dev veth0

	# set the decap route for decapsulating packets arriving from rtsrc
	# and destined to hsdst
	ip -netns "${rtdst_name}" -6 route add "${vpn_sid}/128" \
		table "${LOCALSID_TABLE_ID}" \
		encap seg6local action End.DT46 \
		vrftable "${tid}" dev "vrf-${tid}"

	# all SIDs for VPNs start with a common locator which is fc00::/16.
	# Routes for handling the SRv6 End.DT* behavior instances are grouped
	# together in the 'localsid' table.
	#
	# NOTE: added only once
	if ! ip -netns "${rtdst_name}" -6 rule show | \
	    grep -q "to ${VPN_LOCATOR_SERVICE}::/16 lookup ${LOCALSID_TABLE_ID}"; then
		ip -netns "${rtdst_name}" -6 rule add \
			to "${VPN_LOCATOR_SERVICE}::/16" \
			lookup "${LOCALSID_TABLE_ID}" prio 999
	fi
}

# Configure rtsrc to reach rtdst's loopback address through the VPN.
# args:
#  $1 - router id where the encap route is installed
#  $2 - router id whose loopback is the destination
#  $3 - tenant id
setup_vpn_gw_encap()
{
	local rtsrc="$1"
	local rtdst="$2"
	local tid="$3"
	local sid

	sid="$(build_vpn_sid "${rtsrc}" "${rtdst}" "${tid}")"

	set_gw_encap_route "${rtsrc}" "${rtdst}" "${sid}" "${tid}"
}

setup()
{
	ip link add veth-rt-1 type veth peer name veth-rt-2
	setup_ns rt_1 rt_2
	setup_rt_networking 1
	setup_rt_networking 2

	# setup two hosts for the tenant 100.
	#  - host hs-t100-1 is directly connected to the router rt-1;
	#  - host hs-t100-2 is directly connected to the router rt-2.
	setup_ns hs_t100_1 hs_t100_2
	setup_hs 1 1 100
	setup_hs 2 2 100

	# setup two hosts for the tenant 200.
	#  - host hs-t200-3 is directly connected to the router rt-1;
	#  - host hs-t200-4 is directly connected to the router rt-2.
	setup_ns hs_t200_3 hs_t200_4
	setup_hs 3 1 200
	setup_hs 4 2 200

	# configure each router for each tenant: VRF, blackhole routes,
	# router loopback interface
	setup_rt 1 100
	setup_rt 2 100
	setup_rt 1 200
	setup_rt 2 200

	# setup the L3 VPN which connects the host hs-t100-1 and host hs-t100-2
	# within the same tenant 100.
	setup_vpn_config 1 2 2 100
	setup_vpn_config 2 1 1 100

	# setup the L3 VPN which connects the host hs-t200-3 and host hs-t200-4
	# within the same tenant 200.
	setup_vpn_config 1 4 2 200
	setup_vpn_config 2 3 1 200

	# allow each router to reach the other's loopback through the VPN
	setup_vpn_gw_encap 2 1 100
	setup_vpn_gw_encap 1 2 100
	setup_vpn_gw_encap 2 1 200
	setup_vpn_gw_encap 1 2 200

	# testing environment was set up successfully
	SETUP_ERR=0
}

check_rt_connectivity()
{
	local rtsrc="$1"
	local rtdst="$2"
	local nsname

	nsname="$(get_rt_nsname "${rtsrc}")"

	ip netns exec "${nsname}" ping -c 1 -W 1 "${IPv6_RT_NETWORK}::${rtdst}" \
		>/dev/null 2>&1
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
	local tid="$3"
	local nsname

	nsname="$(get_hs_nsname "${tid}" "${hssrc}")"

	ip netns exec "${nsname}" ping -c 1 -W "${PING_TIMEOUT_SEC}" \
		"${IPv6_HS_NETWORK}::${hsdst}" >/dev/null 2>&1
}

check_hs_ipv4_connectivity()
{
	local hssrc="$1"
	local hsdst="$2"
	local tid="$3"
	local nsname

	nsname="$(get_hs_nsname "${tid}" "${hssrc}")"

	ip netns exec "${nsname}" ping -c 1 -W "${PING_TIMEOUT_SEC}" \
		"${IPv4_HS_NETWORK}.${hsdst}" >/dev/null 2>&1
}

check_and_log_hs_connectivity()
{
	local hssrc="$1"
	local hsdst="$2"
	local tid="$3"

	check_hs_ipv6_connectivity "${hssrc}" "${hsdst}" "${tid}"
	log_test $? 0 "IPv6 connectivity: hs-t${tid}-${hssrc} -> hs-t${tid}-${hsdst} (tenant ${tid})"

	check_hs_ipv4_connectivity "${hssrc}" "${hsdst}" "${tid}"
	log_test $? 0 "IPv4 connectivity: hs-t${tid}-${hssrc} -> hs-t${tid}-${hsdst} (tenant ${tid})"
}

check_and_log_hs_isolation()
{
	local hssrc="$1"
	local tidsrc="$2"
	local hsdst="$3"
	local tiddst="$4"

	check_hs_ipv6_connectivity "${hssrc}" "${hsdst}" "${tidsrc}"
	log_test $? 1 "IPv6 isolation: hs-t${tidsrc}-${hssrc} -X-> hs-t${tiddst}-${hsdst}"

	check_hs_ipv4_connectivity "${hssrc}" "${hsdst}" "${tidsrc}"
	log_test $? 1 "IPv4 isolation: hs-t${tidsrc}-${hssrc} -X-> hs-t${tiddst}-${hsdst}"
}

check_and_log_hs2gw_connectivity()
{
	local hssrc="$1"
	local tid="$2"

	check_hs_ipv6_connectivity "${hssrc}" 254 "${tid}"
	log_test $? 0 "IPv6 connectivity: hs-t${tid}-${hssrc} -> gw (tenant ${tid})"

	check_hs_ipv4_connectivity "${hssrc}" 254 "${tid}"
	log_test $? 0 "IPv4 connectivity: hs-t${tid}-${hssrc} -> gw (tenant ${tid})"
}

router_tests()
{
	log_section "IPv6 routers connectivity test"

	check_and_log_rt_connectivity 1 2
	check_and_log_rt_connectivity 2 1
}

host2gateway_tests()
{
	log_section "Connectivity test among hosts and gateway"

	check_and_log_hs2gw_connectivity 1 100
	check_and_log_hs2gw_connectivity 2 100

	check_and_log_hs2gw_connectivity 3 200
	check_and_log_hs2gw_connectivity 4 200
}

host_vpn_tests()
{
	log_section "SRv6 VPN connectivity test among hosts in the same tenant"

	check_and_log_hs_connectivity 1 2 100
	check_and_log_hs_connectivity 2 1 100

	check_and_log_hs_connectivity 3 4 200
	check_and_log_hs_connectivity 4 3 200
}

host_vpn_isolation_tests()
{
	local l1="1 2"
	local l2="3 4"
	local t1=100
	local t2=200
	local i
	local j
	local tmp

	log_section "SRv6 VPN isolation test among hosts in different tenants"

	for _ in 0 1; do
		for i in ${l1}; do
			for j in ${l2}; do
				check_and_log_hs_isolation "${i}" "${t1}" "${j}" "${t2}"
			done
		done

		# let us test the reverse path
		tmp="${l1}"; l1="${l2}"; l2="${tmp}"
		tmp=${t1}; t1=${t2}; t2=${tmp}
	done
}

__test_nolookup()
{
	local hssrc="$1"
	local hsdst="$2"
	local rtsrc="$3"
	local rtdst="$4"
	local tid="$5"
	local vpn_sid

	vpn_sid="$(build_vpn_sid "${rtsrc}" "${rtdst}" "${tid}")"

	# replace encap route(s) without "lookup" attribute
	set_host_encap_route_nolookup "${rtsrc}" "${hsdst}" "${vpn_sid}" "${tid}"

	check_hs_ipv6_connectivity "${hssrc}" "${hsdst}" "${tid}"
	log_test $? 1 "IPv6 w/o lookup: hs-t${tid}-${hssrc} -X-> hs-t${tid}-${hsdst} (tenant ${tid})"

	check_hs_ipv4_connectivity "${hssrc}" "${hsdst}" "${tid}"
	log_test $? 1 "IPv4 w/o lookup: hs-t${tid}-${hssrc} -X-> hs-t${tid}-${hsdst} (tenant ${tid})"

	# restore encap route(s) with "lookup" for subsequent tests
	set_host_encap_route "${rtsrc}" "${hsdst}" "${vpn_sid}" "${tid}"
}

host_vpn_nolookup_tests()
{
	log_section "SRv6 VPN connectivity test among hosts w/o lookup"

	__test_nolookup 1 2 1 2 100
	__test_nolookup 2 1 2 1 100

	__test_nolookup 3 4 1 2 200
	__test_nolookup 4 3 2 1 200
}

check_gw_ipv6_connectivity()
{
	local rtsrc="$1"
	local rtdst="$2"
	local tidsrc="$3"
	local tiddst="$4"
	local rtname
	local src_v6
	local dst_v6

	rtname="$(get_rt_nsname "${rtsrc}")"
	src_v6="${IPv6_HS_NETWORK}::$(get_rlo_hostid "${rtsrc}" "${tidsrc}")"
	dst_v6="${IPv6_HS_NETWORK}::$(get_rlo_hostid "${rtdst}" "${tiddst}")"

	ip netns exec "${rtname}" ip vrf exec "vrf-${tidsrc}" \
		ping -c 1 -W "${PING_TIMEOUT_SEC}" \
		-I "${src_v6}" "${dst_v6}" >/dev/null 2>&1
}

check_gw_ipv4_connectivity()
{
	local rtsrc="$1"
	local rtdst="$2"
	local tidsrc="$3"
	local tiddst="$4"
	local rtname
	local src_v4
	local dst_v4

	rtname="$(get_rt_nsname "${rtsrc}")"
	src_v4="${IPv4_HS_NETWORK}.$(get_rlo_hostid "${rtsrc}" "${tidsrc}")"
	dst_v4="${IPv4_HS_NETWORK}.$(get_rlo_hostid "${rtdst}" "${tiddst}")"

	ip netns exec "${rtname}" ip vrf exec "vrf-${tidsrc}" \
		ping -c 1 -W "${PING_TIMEOUT_SEC}" \
		-I "${src_v4}" "${dst_v4}" >/dev/null 2>&1
}

check_and_log_gw_connectivity()
{
	local rtsrc="$1"
	local rtdst="$2"
	local tid="$3"

	check_gw_ipv6_connectivity "${rtsrc}" "${rtdst}" "${tid}" "${tid}"
	log_test $? 0 "IPv6 connectivity: rt-${rtsrc} -> rt-${rtdst} (tenant ${tid})"

	check_gw_ipv4_connectivity "${rtsrc}" "${rtdst}" "${tid}" "${tid}"
	log_test $? 0 "IPv4 connectivity: rt-${rtsrc} -> rt-${rtdst} (tenant ${tid})"
}

check_and_log_gw_isolation()
{
	local rtsrc="$1"
	local rtdst="$2"
	local tidsrc="$3"
	local tiddst="$4"

	check_gw_ipv6_connectivity "${rtsrc}" "${rtdst}" "${tidsrc}" "${tiddst}"
	log_test $? 1 "IPv6 isolation: rt-${rtsrc} -X-> rt-${rtdst} (tenants ${tidsrc}/${tiddst})"

	check_gw_ipv4_connectivity "${rtsrc}" "${rtdst}" "${tidsrc}" "${tiddst}"
	log_test $? 1 "IPv4 isolation: rt-${rtsrc} -X-> rt-${rtdst} (tenants ${tidsrc}/${tiddst})"
}

gw_vpn_isolation_tests()
{
	log_section "SRv6 VPN isolation test among routers in different tenants"

	check_and_log_gw_isolation 1 2 100 200
	check_and_log_gw_isolation 2 1 100 200

	check_and_log_gw_isolation 1 2 200 100
	check_and_log_gw_isolation 2 1 200 100
}

gw_vpn_tests()
{
	log_section "SRv6 VPN connectivity test among routers in the same tenant"

	check_and_log_gw_connectivity 1 2 100
	check_and_log_gw_connectivity 2 1 100

	check_and_log_gw_connectivity 1 2 200
	check_and_log_gw_connectivity 2 1 200
}

__test_gw_nolookup()
{
	local rtsrc="$1"
	local rtdst="$2"
	local tid="$3"
	local sid

	sid="$(build_vpn_sid "${rtsrc}" "${rtdst}" "${tid}")"

	# replace gw encap route without "lookup" attribute
	set_gw_encap_route_nolookup "${rtsrc}" "${rtdst}" "${sid}" "${tid}"

	check_gw_ipv6_connectivity "${rtsrc}" "${rtdst}" "${tid}" "${tid}"
	log_test $? 1 "IPv6 w/o lookup: rt-${rtsrc} -X-> rt-${rtdst} (tenant ${tid})"

	check_gw_ipv4_connectivity "${rtsrc}" "${rtdst}" "${tid}" "${tid}"
	log_test $? 1 "IPv4 w/o lookup: rt-${rtsrc} -X-> rt-${rtdst} (tenant ${tid})"

	# restore gw encap route with "lookup" for subsequent tests
	set_gw_encap_route "${rtsrc}" "${rtdst}" "${sid}" "${tid}"
}

gw_vpn_nolookup_tests()
{
	log_section "SRv6 VPN connectivity test among routers w/o lookup"

	__test_gw_nolookup 1 2 100
	__test_gw_nolookup 2 1 100

	__test_gw_nolookup 1 2 200
	__test_gw_nolookup 2 1 200
}

test_command_or_ksft_skip()
{
	local cmd="$1"

	if [ ! -x "$(command -v "${cmd}")" ]; then
		echo "SKIP: Could not run test without \"${cmd}\" tool"
		exit "${ksft_skip}"
	fi
}

test_vrf_or_ksft_skip()
{
	modprobe vrf &>/dev/null || true
	if [ ! -e /proc/sys/net/vrf/strict_mode ]; then
		echo "SKIP: vrf sysctl does not exist"
		exit "${ksft_skip}"
	fi
}

test_dummy_dev_or_ksft_skip()
{
	local test_netns

	setup_ns test_netns

	modprobe dummy &>/dev/null || true
	if ! ip -netns "${test_netns}" link add "${DUMMY_DEVNAME}" \
			type dummy; then
		cleanup_ns "${test_netns}"
		echo "SKIP: dummy dev not supported"
		exit "${ksft_skip}"
	fi

	cleanup_ns "${test_netns}"
}

test_encap_lookup_supp_or_ksft_skip()
{
	local nsname

	setup_ns nsname

	ip -netns "${nsname}" link add "${DUMMY_DEVNAME}" type dummy
	ip -netns "${nsname}" link set "${DUMMY_DEVNAME}" up

	if ! ip -netns "${nsname}" -6 route add "${IPv6_TESTS_ADDR}/128" \
			encap seg6 mode encap segs fc00::1 \
			lookup "${TESTS_TABLE_ID}" \
			dev "${DUMMY_DEVNAME}" 2>/dev/null; then
		cleanup_ns "${nsname}"
		echo "SKIP: seg6 encap lookup attribute not supported"
		exit "${ksft_skip}"
	fi

	# An old kernel with a recent iproute2 accepts the route but
	# silently ignores the lookup attribute. Dump the route and check
	# the attribute is really there, otherwise the test falsely passes.
	if ! ip -netns "${nsname}" -6 route show "${IPv6_TESTS_ADDR}/128" | \
			grep -q "lookup ${TESTS_TABLE_ID}"; then
		cleanup_ns "${nsname}"
		echo "SKIP: seg6 encap lookup attribute not supported"
		exit "${ksft_skip}"
	fi

	cleanup_ns "${nsname}"
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

test_dummy_dev_or_ksft_skip
test_vrf_or_ksft_skip
test_encap_lookup_supp_or_ksft_skip

set -e
trap cleanup EXIT

setup
set +e

router_tests
host2gateway_tests
host_vpn_tests
host_vpn_isolation_tests
host_vpn_nolookup_tests
gw_vpn_tests
gw_vpn_isolation_tests
gw_vpn_nolookup_tests

print_log_test_results
