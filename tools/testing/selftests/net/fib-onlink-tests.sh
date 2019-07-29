#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# IPv4 and IPv6 onlink tests

PAUSE_ON_FAIL=${PAUSE_ON_FAIL:=no}

# Network interfaces
# - odd in current namespace; even in peer ns
declare -A NETIFS
# default VRF
NETIFS[p1]=veth1
NETIFS[p2]=veth2
NETIFS[p3]=veth3
NETIFS[p4]=veth4
# VRF
NETIFS[p5]=veth5
NETIFS[p6]=veth6
NETIFS[p7]=veth7
NETIFS[p8]=veth8

# /24 network
declare -A V4ADDRS
V4ADDRS[p1]=169.254.1.1
V4ADDRS[p2]=169.254.1.2
V4ADDRS[p3]=169.254.3.1
V4ADDRS[p4]=169.254.3.2
V4ADDRS[p5]=169.254.5.1
V4ADDRS[p6]=169.254.5.2
V4ADDRS[p7]=169.254.7.1
V4ADDRS[p8]=169.254.7.2

# /64 network
declare -A V6ADDRS
V6ADDRS[p1]=2001:db8:101::1
V6ADDRS[p2]=2001:db8:101::2
V6ADDRS[p3]=2001:db8:301::1
V6ADDRS[p4]=2001:db8:301::2
V6ADDRS[p5]=2001:db8:501::1
V6ADDRS[p6]=2001:db8:501::2
V6ADDRS[p7]=2001:db8:701::1
V6ADDRS[p8]=2001:db8:701::2

# Test networks:
# [1] = default table
# [2] = VRF
#
# /32 host routes
declare -A TEST_NET4
TEST_NET4[1]=169.254.101
TEST_NET4[2]=169.254.102
# /128 host routes
declare -A TEST_NET6
TEST_NET6[1]=2001:db8:101
TEST_NET6[2]=2001:db8:102

# connected gateway
CONGW[1]=169.254.1.254
CONGW[2]=169.254.3.254
CONGW[3]=169.254.5.254

# recursive gateway
RECGW4[1]=169.254.11.254
RECGW4[2]=169.254.12.254
RECGW6[1]=2001:db8:11::64
RECGW6[2]=2001:db8:12::64

# for v4 mapped to v6
declare -A TEST_NET4IN6IN6
TEST_NET4IN6[1]=10.1.1.254
TEST_NET4IN6[2]=10.2.1.254

# mcast address
MCAST6=ff02::1


PEER_NS=bart
PEER_CMD="ip netns exec ${PEER_NS}"
VRF=lisa
VRF_TABLE=1101
PBR_TABLE=101

################################################################################
# utilities

log_test()
{
	local rc=$1
	local expected=$2
	local msg="$3"

	if [ ${rc} -eq ${expected} ]; then
		nsuccess=$((nsuccess+1))
		printf "\n    TEST: %-50s  [ OK ]\n" "${msg}"
	else
		nfail=$((nfail+1))
		printf "\n    TEST: %-50s  [FAIL]\n" "${msg}"
		if [ "${PAUSE_ON_FAIL}" = "yes" ]; then
			echo
			echo "hit enter to continue, 'q' to quit"
			read a
			[ "$a" = "q" ] && exit 1
		fi
	fi
}

log_section()
{
	echo
	echo "######################################################################"
	echo "TEST SECTION: $*"
	echo "######################################################################"
}

log_subsection()
{
	echo
	echo "#########################################"
	echo "TEST SUBSECTION: $*"
}

run_cmd()
{
	echo
	echo "COMMAND: $*"
	eval $*
}

get_linklocal()
{
	local dev=$1
	local pfx
	local addr

	addr=$(${pfx} ip -6 -br addr show dev ${dev} | \
	awk '{
		for (i = 3; i <= NF; ++i) {
			if ($i ~ /^fe80/)
				print $i
		}
	}'
	)
	addr=${addr/\/*}

	[ -z "$addr" ] && return 1

	echo $addr

	return 0
}

################################################################################
#

setup()
{
	echo
	echo "########################################"
	echo "Configuring interfaces"

	set -e

	# create namespace
	ip netns add ${PEER_NS}
	ip -netns ${PEER_NS} li set lo up

	# add vrf table
	ip li add ${VRF} type vrf table ${VRF_TABLE}
	ip li set ${VRF} up
	ip ro add table ${VRF_TABLE} unreachable default metric 8192
	ip -6 ro add table ${VRF_TABLE} unreachable default metric 8192

	# create test interfaces
	ip li add ${NETIFS[p1]} type veth peer name ${NETIFS[p2]}
	ip li add ${NETIFS[p3]} type veth peer name ${NETIFS[p4]}
	ip li add ${NETIFS[p5]} type veth peer name ${NETIFS[p6]}
	ip li add ${NETIFS[p7]} type veth peer name ${NETIFS[p8]}

	# enslave vrf interfaces
	for n in 5 7; do
		ip li set ${NETIFS[p${n}]} vrf ${VRF}
	done

	# add addresses
	for n in 1 3 5 7; do
		ip li set ${NETIFS[p${n}]} up
		ip addr add ${V4ADDRS[p${n}]}/24 dev ${NETIFS[p${n}]}
		ip addr add ${V6ADDRS[p${n}]}/64 dev ${NETIFS[p${n}]} nodad
	done

	# move peer interfaces to namespace and add addresses
	for n in 2 4 6 8; do
		ip li set ${NETIFS[p${n}]} netns ${PEER_NS} up
		ip -netns ${PEER_NS} addr add ${V4ADDRS[p${n}]}/24 dev ${NETIFS[p${n}]}
		ip -netns ${PEER_NS} addr add ${V6ADDRS[p${n}]}/64 dev ${NETIFS[p${n}]} nodad
	done

	ip -6 ro add default via ${V6ADDRS[p3]/::[0-9]/::64}
	ip -6 ro add table ${VRF_TABLE} default via ${V6ADDRS[p7]/::[0-9]/::64}

	set +e
}

cleanup()
{
	# make sure we start from a clean slate
	ip netns del ${PEER_NS} 2>/dev/null
	for n in 1 3 5 7; do
		ip link del ${NETIFS[p${n}]} 2>/dev/null
	done
	ip link del ${VRF} 2>/dev/null
	ip ro flush table ${VRF_TABLE}
	ip -6 ro flush table ${VRF_TABLE}
}

################################################################################
# IPv4 tests
#

run_ip()
{
	local table="$1"
	local prefix="$2"
	local gw="$3"
	local dev="$4"
	local exp_rc="$5"
	local desc="$6"

	# dev arg may be empty
	[ -n "${dev}" ] && dev="dev ${dev}"

	run_cmd ip ro add table "${table}" "${prefix}"/32 via "${gw}" "${dev}" onlink
	log_test $? ${exp_rc} "${desc}"
}

run_ip_mpath()
{
	local table="$1"
	local prefix="$2"
	local nh1="$3"
	local nh2="$4"
	local exp_rc="$5"
	local desc="$6"

	# dev arg may be empty
	[ -n "${dev}" ] && dev="dev ${dev}"

	run_cmd ip ro add table "${table}" "${prefix}"/32 \
		nexthop via ${nh1} nexthop via ${nh2}
	log_test $? ${exp_rc} "${desc}"
}

valid_onlink_ipv4()
{
	# - unicast connected, unicast recursive
	#
	log_subsection "default VRF - main table"

	run_ip 254 ${TEST_NET4[1]}.1 ${CONGW[1]} ${NETIFS[p1]} 0 "unicast connected"
	run_ip 254 ${TEST_NET4[1]}.2 ${RECGW4[1]} ${NETIFS[p1]} 0 "unicast recursive"

	log_subsection "VRF ${VRF}"

	run_ip ${VRF_TABLE} ${TEST_NET4[2]}.1 ${CONGW[3]} ${NETIFS[p5]} 0 "unicast connected"
	run_ip ${VRF_TABLE} ${TEST_NET4[2]}.2 ${RECGW4[2]} ${NETIFS[p5]} 0 "unicast recursive"

	log_subsection "VRF device, PBR table"

	run_ip ${PBR_TABLE} ${TEST_NET4[2]}.3 ${CONGW[3]} ${NETIFS[p5]} 0 "unicast connected"
	run_ip ${PBR_TABLE} ${TEST_NET4[2]}.4 ${RECGW4[2]} ${NETIFS[p5]} 0 "unicast recursive"

	# multipath version
	#
	log_subsection "default VRF - main table - multipath"

	run_ip_mpath 254 ${TEST_NET4[1]}.5 \
		"${CONGW[1]} dev ${NETIFS[p1]} onlink" \
		"${CONGW[2]} dev ${NETIFS[p3]} onlink" \
		0 "unicast connected - multipath"

	run_ip_mpath 254 ${TEST_NET4[1]}.6 \
		"${RECGW4[1]} dev ${NETIFS[p1]} onlink" \
		"${RECGW4[2]} dev ${NETIFS[p3]} onlink" \
		0 "unicast recursive - multipath"

	run_ip_mpath 254 ${TEST_NET4[1]}.7 \
		"${CONGW[1]} dev ${NETIFS[p1]}"        \
		"${CONGW[2]} dev ${NETIFS[p3]} onlink" \
		0 "unicast connected - multipath onlink first only"

	run_ip_mpath 254 ${TEST_NET4[1]}.8 \
		"${CONGW[1]} dev ${NETIFS[p1]} onlink" \
		"${CONGW[2]} dev ${NETIFS[p3]}"        \
		0 "unicast connected - multipath onlink second only"
}

invalid_onlink_ipv4()
{
	run_ip 254 ${TEST_NET4[1]}.11 ${V4ADDRS[p1]} ${NETIFS[p1]} 2 \
		"Invalid gw - local unicast address"

	run_ip ${VRF_TABLE} ${TEST_NET4[2]}.11 ${V4ADDRS[p5]} ${NETIFS[p5]} 2 \
		"Invalid gw - local unicast address, VRF"

	run_ip 254 ${TEST_NET4[1]}.101 ${V4ADDRS[p1]} "" 2 "No nexthop device given"

	run_ip 254 ${TEST_NET4[1]}.102 ${V4ADDRS[p3]} ${NETIFS[p1]} 2 \
		"Gateway resolves to wrong nexthop device"

	run_ip ${VRF_TABLE} ${TEST_NET4[2]}.103 ${V4ADDRS[p7]} ${NETIFS[p5]} 2 \
		"Gateway resolves to wrong nexthop device - VRF"
}

################################################################################
# IPv6 tests
#

run_ip6()
{
	local table="$1"
	local prefix="$2"
	local gw="$3"
	local dev="$4"
	local exp_rc="$5"
	local desc="$6"

	# dev arg may be empty
	[ -n "${dev}" ] && dev="dev ${dev}"

	run_cmd ip -6 ro add table "${table}" "${prefix}"/128 via "${gw}" "${dev}" onlink
	log_test $? ${exp_rc} "${desc}"
}

run_ip6_mpath()
{
	local table="$1"
	local prefix="$2"
	local opts="$3"
	local nh1="$4"
	local nh2="$5"
	local exp_rc="$6"
	local desc="$7"

	run_cmd ip -6 ro add table "${table}" "${prefix}"/128 "${opts}" \
		nexthop via ${nh1} nexthop via ${nh2}
	log_test $? ${exp_rc} "${desc}"
}

valid_onlink_ipv6()
{
	# - unicast connected, unicast recursive, v4-mapped
	#
	log_subsection "default VRF - main table"

	run_ip6 254 ${TEST_NET6[1]}::1 ${V6ADDRS[p1]/::*}::64 ${NETIFS[p1]} 0 "unicast connected"
	run_ip6 254 ${TEST_NET6[1]}::2 ${RECGW6[1]} ${NETIFS[p1]} 0 "unicast recursive"
	run_ip6 254 ${TEST_NET6[1]}::3 ::ffff:${TEST_NET4IN6[1]} ${NETIFS[p1]} 0 "v4-mapped"

	log_subsection "VRF ${VRF}"

	run_ip6 ${VRF_TABLE} ${TEST_NET6[2]}::1 ${V6ADDRS[p5]/::*}::64 ${NETIFS[p5]} 0 "unicast connected"
	run_ip6 ${VRF_TABLE} ${TEST_NET6[2]}::2 ${RECGW6[2]} ${NETIFS[p5]} 0 "unicast recursive"
	run_ip6 ${VRF_TABLE} ${TEST_NET6[2]}::3 ::ffff:${TEST_NET4IN6[2]} ${NETIFS[p5]} 0 "v4-mapped"

	log_subsection "VRF device, PBR table"

	run_ip6 ${PBR_TABLE} ${TEST_NET6[2]}::4 ${V6ADDRS[p5]/::*}::64 ${NETIFS[p5]} 0 "unicast connected"
	run_ip6 ${PBR_TABLE} ${TEST_NET6[2]}::5 ${RECGW6[2]} ${NETIFS[p5]} 0 "unicast recursive"
	run_ip6 ${PBR_TABLE} ${TEST_NET6[2]}::6 ::ffff:${TEST_NET4IN6[2]} ${NETIFS[p5]} 0 "v4-mapped"

	# multipath version
	#
	log_subsection "default VRF - main table - multipath"

	run_ip6_mpath 254 ${TEST_NET6[1]}::4 "onlink" \
		"${V6ADDRS[p1]/::*}::64 dev ${NETIFS[p1]}" \
		"${V6ADDRS[p3]/::*}::64 dev ${NETIFS[p3]}" \
		0 "unicast connected - multipath onlink"

	run_ip6_mpath 254 ${TEST_NET6[1]}::5 "onlink" \
		"${RECGW6[1]} dev ${NETIFS[p1]}" \
		"${RECGW6[2]} dev ${NETIFS[p3]}" \
		0 "unicast recursive - multipath onlink"

	run_ip6_mpath 254 ${TEST_NET6[1]}::6 "onlink" \
		"::ffff:${TEST_NET4IN6[1]} dev ${NETIFS[p1]}" \
		"::ffff:${TEST_NET4IN6[2]} dev ${NETIFS[p3]}" \
		0 "v4-mapped - multipath onlink"

	run_ip6_mpath 254 ${TEST_NET6[1]}::7 "" \
		"${V6ADDRS[p1]/::*}::64 dev ${NETIFS[p1]} onlink" \
		"${V6ADDRS[p3]/::*}::64 dev ${NETIFS[p3]} onlink" \
		0 "unicast connected - multipath onlink both nexthops"

	run_ip6_mpath 254 ${TEST_NET6[1]}::8 "" \
		"${V6ADDRS[p1]/::*}::64 dev ${NETIFS[p1]} onlink" \
		"${V6ADDRS[p3]/::*}::64 dev ${NETIFS[p3]}" \
		0 "unicast connected - multipath onlink first only"

	run_ip6_mpath 254 ${TEST_NET6[1]}::9 "" \
		"${V6ADDRS[p1]/::*}::64 dev ${NETIFS[p1]}"        \
		"${V6ADDRS[p3]/::*}::64 dev ${NETIFS[p3]} onlink" \
		0 "unicast connected - multipath onlink second only"
}

invalid_onlink_ipv6()
{
	local lladdr

	lladdr=$(get_linklocal ${NETIFS[p1]}) || return 1

	run_ip6 254 ${TEST_NET6[1]}::11 ${V6ADDRS[p1]} ${NETIFS[p1]} 2 \
		"Invalid gw - local unicast address"
	run_ip6 254 ${TEST_NET6[1]}::12 ${lladdr} ${NETIFS[p1]} 2 \
		"Invalid gw - local linklocal address"
	run_ip6 254 ${TEST_NET6[1]}::12 ${MCAST6} ${NETIFS[p1]} 2 \
		"Invalid gw - multicast address"

	lladdr=$(get_linklocal ${NETIFS[p5]}) || return 1
	run_ip6 ${VRF_TABLE} ${TEST_NET6[2]}::11 ${V6ADDRS[p5]} ${NETIFS[p5]} 2 \
		"Invalid gw - local unicast address, VRF"
	run_ip6 ${VRF_TABLE} ${TEST_NET6[2]}::12 ${lladdr} ${NETIFS[p5]} 2 \
		"Invalid gw - local linklocal address, VRF"
	run_ip6 ${VRF_TABLE} ${TEST_NET6[2]}::12 ${MCAST6} ${NETIFS[p5]} 2 \
		"Invalid gw - multicast address, VRF"

	run_ip6 254 ${TEST_NET6[1]}::101 ${V6ADDRS[p1]} "" 2 \
		"No nexthop device given"

	# default VRF validation is done against LOCAL table
	# run_ip6 254 ${TEST_NET6[1]}::102 ${V6ADDRS[p3]/::[0-9]/::64} ${NETIFS[p1]} 2 \
	#	"Gateway resolves to wrong nexthop device"

	run_ip6 ${VRF_TABLE} ${TEST_NET6[2]}::103 ${V6ADDRS[p7]/::[0-9]/::64} ${NETIFS[p5]} 2 \
		"Gateway resolves to wrong nexthop device - VRF"
}

run_onlink_tests()
{
	log_section "IPv4 onlink"
	log_subsection "Valid onlink commands"
	valid_onlink_ipv4
	log_subsection "Invalid onlink commands"
	invalid_onlink_ipv4

	log_section "IPv6 onlink"
	log_subsection "Valid onlink commands"
	valid_onlink_ipv6
	log_subsection "Invalid onlink commands"
	invalid_onlink_ipv6
}

################################################################################
# main

nsuccess=0
nfail=0

cleanup
setup
run_onlink_tests
cleanup

if [ "$TESTS" != "none" ]; then
	printf "\nTests passed: %3d\n" ${nsuccess}
	printf "Tests failed: %3d\n"   ${nfail}
fi
