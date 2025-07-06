#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

source ./lib.sh

PAUSE_ON_FAIL="no"

# The trap function handler
#
exit_cleanup_all()
{
	cleanup_all_ns

	exit "${EXIT_STATUS}"
}

# Add fake IPv4 and IPv6 networks on the loopback device, to be used as
# underlay by future GRE devices.
#
setup_basenet()
{
	ip -netns "${NS0}" link set dev lo up
	ip -netns "${NS0}" address add dev lo 192.0.2.10/24
	ip -netns "${NS0}" address add dev lo 2001:db8::10/64 nodad
}

# Check if network device has an IPv6 link-local address assigned.
#
# Parameters:
#
#   * $1: The network device to test
#   * $2: An extra regular expression that should be matched (to verify the
#         presence of extra attributes)
#   * $3: The expected return code from grep (to allow checking the absence of
#         a link-local address)
#   * $4: The user visible name for the scenario being tested
#
check_ipv6_ll_addr()
{
	local DEV="$1"
	local EXTRA_MATCH="$2"
	local XRET="$3"
	local MSG="$4"

	RET=0
	set +e
	ip -netns "${NS0}" -6 address show dev "${DEV}" scope link | grep "fe80::" | grep -q "${EXTRA_MATCH}"
	check_err_fail "${XRET}" $? ""
	log_test "${MSG}"
	set -e
}

# Create a GRE device and verify that it gets an IPv6 link-local address as
# expected.
#
# Parameters:
#
#   * $1: The device type (gre, ip6gre, gretap or ip6gretap)
#   * $2: The local underlay IP address (can be an IPv4, an IPv6 or "any")
#   * $3: The remote underlay IP address (can be an IPv4, an IPv6 or "any")
#   * $4: The IPv6 interface identifier generation mode to use for the GRE
#         device (eui64, none, stable-privacy or random).
#
test_gre_device()
{
	local GRE_TYPE="$1"
	local LOCAL_IP="$2"
	local REMOTE_IP="$3"
	local MODE="$4"
	local ADDR_GEN_MODE
	local MATCH_REGEXP
	local MSG

	ip link add netns "${NS0}" name gretest type "${GRE_TYPE}" local "${LOCAL_IP}" remote "${REMOTE_IP}"

	case "${MODE}" in
	    "eui64")
		ADDR_GEN_MODE=0
		MATCH_REGEXP=""
		MSG="${GRE_TYPE}, mode: 0 (EUI64), ${LOCAL_IP} -> ${REMOTE_IP}"
		XRET=0
		;;
	    "none")
		ADDR_GEN_MODE=1
		MATCH_REGEXP=""
		MSG="${GRE_TYPE}, mode: 1 (none), ${LOCAL_IP} -> ${REMOTE_IP}"
		XRET=1 # No link-local address should be generated
		;;
	    "stable-privacy")
		ADDR_GEN_MODE=2
		MATCH_REGEXP="stable-privacy"
		MSG="${GRE_TYPE}, mode: 2 (stable privacy), ${LOCAL_IP} -> ${REMOTE_IP}"
		XRET=0
		# Initialise stable_secret (required for stable-privacy mode)
		ip netns exec "${NS0}" sysctl -qw net.ipv6.conf.gretest.stable_secret="2001:db8::abcd"
		;;
	    "random")
		ADDR_GEN_MODE=3
		MATCH_REGEXP="stable-privacy"
		MSG="${GRE_TYPE}, mode: 3 (random), ${LOCAL_IP} -> ${REMOTE_IP}"
		XRET=0
		;;
	esac

	# Check that IPv6 link-local address is generated when device goes up
	ip netns exec "${NS0}" sysctl -qw net.ipv6.conf.gretest.addr_gen_mode="${ADDR_GEN_MODE}"
	ip -netns "${NS0}" link set dev gretest up
	check_ipv6_ll_addr gretest "${MATCH_REGEXP}" "${XRET}" "config: ${MSG}"

	# Now disable link-local address generation
	ip -netns "${NS0}" link set dev gretest down
	ip netns exec "${NS0}" sysctl -qw net.ipv6.conf.gretest.addr_gen_mode=1
	ip -netns "${NS0}" link set dev gretest up

	# Check that link-local address generation works when re-enabled while
	# the device is already up
	ip netns exec "${NS0}" sysctl -qw net.ipv6.conf.gretest.addr_gen_mode="${ADDR_GEN_MODE}"
	check_ipv6_ll_addr gretest "${MATCH_REGEXP}" "${XRET}" "update: ${MSG}"

	ip -netns "${NS0}" link del dev gretest
}

test_gre4()
{
	local GRE_TYPE
	local MODE

	for GRE_TYPE in "gre" "gretap"; do
		printf "\n####\nTesting IPv6 link-local address generation on ${GRE_TYPE} devices\n####\n\n"

		for MODE in "eui64" "none" "stable-privacy" "random"; do
			test_gre_device "${GRE_TYPE}" 192.0.2.10 192.0.2.11 "${MODE}"
			test_gre_device "${GRE_TYPE}" any 192.0.2.11 "${MODE}"
			test_gre_device "${GRE_TYPE}" 192.0.2.10 any "${MODE}"
		done
	done
}

test_gre6()
{
	local GRE_TYPE
	local MODE

	for GRE_TYPE in "ip6gre" "ip6gretap"; do
		printf "\n####\nTesting IPv6 link-local address generation on ${GRE_TYPE} devices\n####\n\n"

		for MODE in "eui64" "none" "stable-privacy" "random"; do
			test_gre_device "${GRE_TYPE}" 2001:db8::10 2001:db8::11 "${MODE}"
			test_gre_device "${GRE_TYPE}" any 2001:db8::11 "${MODE}"
			test_gre_device "${GRE_TYPE}" 2001:db8::10 any "${MODE}"
		done
	done
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

setup_ns NS0

set -e
trap exit_cleanup_all EXIT

setup_basenet

test_gre4
test_gre6
