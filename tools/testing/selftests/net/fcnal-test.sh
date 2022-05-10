#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2019 David Ahern <dsahern@gmail.com>. All rights reserved.
#
# IPv4 and IPv6 functional tests focusing on VRF and routing lookups
# for various permutations:
#   1. icmp, tcp, udp and netfilter
#   2. client, server, no-server
#   3. global address on interface
#   4. global address on 'lo'
#   5. remote and local traffic
#   6. VRF and non-VRF permutations
#
# Setup:
#                     ns-A     |     ns-B
# No VRF case:
#    [ lo ]         [ eth1 ]---|---[ eth1 ]      [ lo ]
#                                                remote address
# VRF case:
#         [ red ]---[ eth1 ]---|---[ eth1 ]      [ lo ]
#
# ns-A:
#     eth1: 172.16.1.1/24, 2001:db8:1::1/64
#       lo: 127.0.0.1/8, ::1/128
#           172.16.2.1/32, 2001:db8:2::1/128
#      red: 127.0.0.1/8, ::1/128
#           172.16.3.1/32, 2001:db8:3::1/128
#
# ns-B:
#     eth1: 172.16.1.2/24, 2001:db8:1::2/64
#      lo2: 127.0.0.1/8, ::1/128
#           172.16.2.2/32, 2001:db8:2::2/128
#
# ns-A to ns-C connection - only for VRF and same config
# as ns-A to ns-B
#
# server / client nomenclature relative to ns-A

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

VERBOSE=0

NSA_DEV=eth1
NSA_DEV2=eth2
NSB_DEV=eth1
NSC_DEV=eth2
VRF=red
VRF_TABLE=1101

# IPv4 config
NSA_IP=172.16.1.1
NSB_IP=172.16.1.2
VRF_IP=172.16.3.1
NS_NET=172.16.1.0/24

# IPv6 config
NSA_IP6=2001:db8:1::1
NSB_IP6=2001:db8:1::2
VRF_IP6=2001:db8:3::1
NS_NET6=2001:db8:1::/120

NSA_LO_IP=172.16.2.1
NSB_LO_IP=172.16.2.2
NSA_LO_IP6=2001:db8:2::1
NSB_LO_IP6=2001:db8:2::2

MD5_PW=abc123
MD5_WRONG_PW=abc1234

MCAST=ff02::1
# set after namespace create
NSA_LINKIP6=
NSB_LINKIP6=

NSA=ns-A
NSB=ns-B
NSC=ns-C

NSA_CMD="ip netns exec ${NSA}"
NSB_CMD="ip netns exec ${NSB}"
NSC_CMD="ip netns exec ${NSC}"

which ping6 > /dev/null 2>&1 && ping6=$(which ping6) || ping6=$(which ping)

################################################################################
# utilities

log_test()
{
	local rc=$1
	local expected=$2
	local msg="$3"

	[ "${VERBOSE}" = "1" ] && echo

	if [ ${rc} -eq ${expected} ]; then
		nsuccess=$((nsuccess+1))
		printf "TEST: %-70s  [ OK ]\n" "${msg}"
	else
		nfail=$((nfail+1))
		printf "TEST: %-70s  [FAIL]\n" "${msg}"
		if [ "${PAUSE_ON_FAIL}" = "yes" ]; then
			echo
			echo "hit enter to continue, 'q' to quit"
			read a
			[ "$a" = "q" ] && exit 1
		fi
	fi

	if [ "${PAUSE}" = "yes" ]; then
		echo
		echo "hit enter to continue, 'q' to quit"
		read a
		[ "$a" = "q" ] && exit 1
	fi

	kill_procs
}

log_test_addr()
{
	local addr=$1
	local rc=$2
	local expected=$3
	local msg="$4"
	local astr

	astr=$(addr2str ${addr})
	log_test $rc $expected "$msg - ${astr}"
}

log_section()
{
	echo
	echo "###########################################################################"
	echo "$*"
	echo "###########################################################################"
	echo
}

log_subsection()
{
	echo
	echo "#################################################################"
	echo "$*"
	echo
}

log_start()
{
	# make sure we have no test instances running
	kill_procs

	if [ "${VERBOSE}" = "1" ]; then
		echo
		echo "#######################################################"
	fi
}

log_debug()
{
	if [ "${VERBOSE}" = "1" ]; then
		echo
		echo "$*"
		echo
	fi
}

show_hint()
{
	if [ "${VERBOSE}" = "1" ]; then
		echo "HINT: $*"
		echo
	fi
}

kill_procs()
{
	killall nettest ping ping6 >/dev/null 2>&1
	sleep 1
}

do_run_cmd()
{
	local cmd="$*"
	local out

	if [ "$VERBOSE" = "1" ]; then
		echo "COMMAND: ${cmd}"
	fi

	out=$($cmd 2>&1)
	rc=$?
	if [ "$VERBOSE" = "1" -a -n "$out" ]; then
		echo "$out"
	fi

	return $rc
}

run_cmd()
{
	do_run_cmd ${NSA_CMD} $*
}

run_cmd_nsb()
{
	do_run_cmd ${NSB_CMD} $*
}

run_cmd_nsc()
{
	do_run_cmd ${NSC_CMD} $*
}

setup_cmd()
{
	local cmd="$*"
	local rc

	run_cmd ${cmd}
	rc=$?
	if [ $rc -ne 0 ]; then
		# show user the command if not done so already
		if [ "$VERBOSE" = "0" ]; then
			echo "setup command: $cmd"
		fi
		echo "failed. stopping tests"
		if [ "${PAUSE_ON_FAIL}" = "yes" ]; then
			echo
			echo "hit enter to continue"
			read a
		fi
		exit $rc
	fi
}

setup_cmd_nsb()
{
	local cmd="$*"
	local rc

	run_cmd_nsb ${cmd}
	rc=$?
	if [ $rc -ne 0 ]; then
		# show user the command if not done so already
		if [ "$VERBOSE" = "0" ]; then
			echo "setup command: $cmd"
		fi
		echo "failed. stopping tests"
		if [ "${PAUSE_ON_FAIL}" = "yes" ]; then
			echo
			echo "hit enter to continue"
			read a
		fi
		exit $rc
	fi
}

setup_cmd_nsc()
{
	local cmd="$*"
	local rc

	run_cmd_nsc ${cmd}
	rc=$?
	if [ $rc -ne 0 ]; then
		# show user the command if not done so already
		if [ "$VERBOSE" = "0" ]; then
			echo "setup command: $cmd"
		fi
		echo "failed. stopping tests"
		if [ "${PAUSE_ON_FAIL}" = "yes" ]; then
			echo
			echo "hit enter to continue"
			read a
		fi
		exit $rc
	fi
}

# set sysctl values in NS-A
set_sysctl()
{
	echo "SYSCTL: $*"
	echo
	run_cmd sysctl -q -w $*
}

# get sysctl values in NS-A
get_sysctl()
{
	${NSA_CMD} sysctl -n $*
}

################################################################################
# Setup for tests

addr2str()
{
	case "$1" in
	127.0.0.1) echo "loopback";;
	::1) echo "IPv6 loopback";;

	${NSA_IP})	echo "ns-A IP";;
	${NSA_IP6})	echo "ns-A IPv6";;
	${NSA_LO_IP})	echo "ns-A loopback IP";;
	${NSA_LO_IP6})	echo "ns-A loopback IPv6";;
	${NSA_LINKIP6}|${NSA_LINKIP6}%*) echo "ns-A IPv6 LLA";;

	${NSB_IP})	echo "ns-B IP";;
	${NSB_IP6})	echo "ns-B IPv6";;
	${NSB_LO_IP})	echo "ns-B loopback IP";;
	${NSB_LO_IP6})	echo "ns-B loopback IPv6";;
	${NSB_LINKIP6}|${NSB_LINKIP6}%*) echo "ns-B IPv6 LLA";;

	${VRF_IP})	echo "VRF IP";;
	${VRF_IP6})	echo "VRF IPv6";;

	${MCAST}%*)	echo "multicast IP";;

	*) echo "unknown";;
	esac
}

get_linklocal()
{
	local ns=$1
	local dev=$2
	local addr

	addr=$(ip -netns ${ns} -6 -br addr show dev ${dev} | \
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
# create namespaces and vrf

create_vrf()
{
	local ns=$1
	local vrf=$2
	local table=$3
	local addr=$4
	local addr6=$5

	ip -netns ${ns} link add ${vrf} type vrf table ${table}
	ip -netns ${ns} link set ${vrf} up
	ip -netns ${ns} route add vrf ${vrf} unreachable default metric 8192
	ip -netns ${ns} -6 route add vrf ${vrf} unreachable default metric 8192

	ip -netns ${ns} addr add 127.0.0.1/8 dev ${vrf}
	ip -netns ${ns} -6 addr add ::1 dev ${vrf} nodad
	if [ "${addr}" != "-" ]; then
		ip -netns ${ns} addr add dev ${vrf} ${addr}
	fi
	if [ "${addr6}" != "-" ]; then
		ip -netns ${ns} -6 addr add dev ${vrf} ${addr6}
	fi

	ip -netns ${ns} ru del pref 0
	ip -netns ${ns} ru add pref 32765 from all lookup local
	ip -netns ${ns} -6 ru del pref 0
	ip -netns ${ns} -6 ru add pref 32765 from all lookup local
}

create_ns()
{
	local ns=$1
	local addr=$2
	local addr6=$3

	ip netns add ${ns}

	ip -netns ${ns} link set lo up
	if [ "${addr}" != "-" ]; then
		ip -netns ${ns} addr add dev lo ${addr}
	fi
	if [ "${addr6}" != "-" ]; then
		ip -netns ${ns} -6 addr add dev lo ${addr6}
	fi

	ip -netns ${ns} ro add unreachable default metric 8192
	ip -netns ${ns} -6 ro add unreachable default metric 8192

	ip netns exec ${ns} sysctl -qw net.ipv4.ip_forward=1
	ip netns exec ${ns} sysctl -qw net.ipv6.conf.all.keep_addr_on_down=1
	ip netns exec ${ns} sysctl -qw net.ipv6.conf.all.forwarding=1
	ip netns exec ${ns} sysctl -qw net.ipv6.conf.default.forwarding=1
}

# create veth pair to connect namespaces and apply addresses.
connect_ns()
{
	local ns1=$1
	local ns1_dev=$2
	local ns1_addr=$3
	local ns1_addr6=$4
	local ns2=$5
	local ns2_dev=$6
	local ns2_addr=$7
	local ns2_addr6=$8

	ip -netns ${ns1} li add ${ns1_dev} type veth peer name tmp
	ip -netns ${ns1} li set ${ns1_dev} up
	ip -netns ${ns1} li set tmp netns ${ns2} name ${ns2_dev}
	ip -netns ${ns2} li set ${ns2_dev} up

	if [ "${ns1_addr}" != "-" ]; then
		ip -netns ${ns1} addr add dev ${ns1_dev} ${ns1_addr}
		ip -netns ${ns2} addr add dev ${ns2_dev} ${ns2_addr}
	fi

	if [ "${ns1_addr6}" != "-" ]; then
		ip -netns ${ns1} addr add dev ${ns1_dev} ${ns1_addr6}
		ip -netns ${ns2} addr add dev ${ns2_dev} ${ns2_addr6}
	fi
}

cleanup()
{
	# explicit cleanups to check those code paths
	ip netns | grep -q ${NSA}
	if [ $? -eq 0 ]; then
		ip -netns ${NSA} link delete ${VRF}
		ip -netns ${NSA} ro flush table ${VRF_TABLE}

		ip -netns ${NSA} addr flush dev ${NSA_DEV}
		ip -netns ${NSA} -6 addr flush dev ${NSA_DEV}
		ip -netns ${NSA} link set dev ${NSA_DEV} down
		ip -netns ${NSA} link del dev ${NSA_DEV}

		ip netns pids ${NSA} | xargs kill 2>/dev/null
		ip netns del ${NSA}
	fi

	ip netns pids ${NSB} | xargs kill 2>/dev/null
	ip netns del ${NSB}
	ip netns pids ${NSC} | xargs kill 2>/dev/null
	ip netns del ${NSC} >/dev/null 2>&1
}

cleanup_vrf_dup()
{
	ip link del ${NSA_DEV2} >/dev/null 2>&1
	ip netns pids ${NSC} | xargs kill 2>/dev/null
	ip netns del ${NSC} >/dev/null 2>&1
}

setup_vrf_dup()
{
	# some VRF tests use ns-C which has the same config as
	# ns-B but for a device NOT in the VRF
	create_ns ${NSC} "-" "-"
	connect_ns ${NSA} ${NSA_DEV2} ${NSA_IP}/24 ${NSA_IP6}/64 \
		   ${NSC} ${NSC_DEV} ${NSB_IP}/24 ${NSB_IP6}/64
}

setup()
{
	local with_vrf=${1}

	# make sure we are starting with a clean slate
	kill_procs
	cleanup 2>/dev/null

	log_debug "Configuring network namespaces"
	set -e

	create_ns ${NSA} ${NSA_LO_IP}/32 ${NSA_LO_IP6}/128
	create_ns ${NSB} ${NSB_LO_IP}/32 ${NSB_LO_IP6}/128
	connect_ns ${NSA} ${NSA_DEV} ${NSA_IP}/24 ${NSA_IP6}/64 \
		   ${NSB} ${NSB_DEV} ${NSB_IP}/24 ${NSB_IP6}/64

	NSA_LINKIP6=$(get_linklocal ${NSA} ${NSA_DEV})
	NSB_LINKIP6=$(get_linklocal ${NSB} ${NSB_DEV})

	# tell ns-A how to get to remote addresses of ns-B
	if [ "${with_vrf}" = "yes" ]; then
		create_vrf ${NSA} ${VRF} ${VRF_TABLE} ${VRF_IP} ${VRF_IP6}

		ip -netns ${NSA} link set dev ${NSA_DEV} vrf ${VRF}
		ip -netns ${NSA} ro add vrf ${VRF} ${NSB_LO_IP}/32 via ${NSB_IP} dev ${NSA_DEV}
		ip -netns ${NSA} -6 ro add vrf ${VRF} ${NSB_LO_IP6}/128 via ${NSB_IP6} dev ${NSA_DEV}

		ip -netns ${NSB} ro add ${VRF_IP}/32 via ${NSA_IP} dev ${NSB_DEV}
		ip -netns ${NSB} -6 ro add ${VRF_IP6}/128 via ${NSA_IP6} dev ${NSB_DEV}
	else
		ip -netns ${NSA} ro add ${NSB_LO_IP}/32 via ${NSB_IP} dev ${NSA_DEV}
		ip -netns ${NSA} ro add ${NSB_LO_IP6}/128 via ${NSB_IP6} dev ${NSA_DEV}
	fi


	# tell ns-B how to get to remote addresses of ns-A
	ip -netns ${NSB} ro add ${NSA_LO_IP}/32 via ${NSA_IP} dev ${NSB_DEV}
	ip -netns ${NSB} ro add ${NSA_LO_IP6}/128 via ${NSA_IP6} dev ${NSB_DEV}

	set +e

	sleep 1
}

setup_lla_only()
{
	# make sure we are starting with a clean slate
	kill_procs
	cleanup 2>/dev/null

	log_debug "Configuring network namespaces"
	set -e

	create_ns ${NSA} "-" "-"
	create_ns ${NSB} "-" "-"
	create_ns ${NSC} "-" "-"
	connect_ns ${NSA} ${NSA_DEV} "-" "-" \
		   ${NSB} ${NSB_DEV} "-" "-"
	connect_ns ${NSA} ${NSA_DEV2} "-" "-" \
		   ${NSC} ${NSC_DEV}  "-" "-"

	NSA_LINKIP6=$(get_linklocal ${NSA} ${NSA_DEV})
	NSB_LINKIP6=$(get_linklocal ${NSB} ${NSB_DEV})
	NSC_LINKIP6=$(get_linklocal ${NSC} ${NSC_DEV})

	create_vrf ${NSA} ${VRF} ${VRF_TABLE} "-" "-"
	ip -netns ${NSA} link set dev ${NSA_DEV} vrf ${VRF}
	ip -netns ${NSA} link set dev ${NSA_DEV2} vrf ${VRF}

	set +e

	sleep 1
}

################################################################################
# IPv4

ipv4_ping_novrf()
{
	local a

	#
	# out
	#
	for a in ${NSB_IP} ${NSB_LO_IP}
	do
		log_start
		run_cmd ping -c1 -w1 ${a}
		log_test_addr ${a} $? 0 "ping out"

		log_start
		run_cmd ping -c1 -w1 -I ${NSA_DEV} ${a}
		log_test_addr ${a} $? 0 "ping out, device bind"

		log_start
		run_cmd ping -c1 -w1 -I ${NSA_LO_IP} ${a}
		log_test_addr ${a} $? 0 "ping out, address bind"
	done

	#
	# in
	#
	for a in ${NSA_IP} ${NSA_LO_IP}
	do
		log_start
		run_cmd_nsb ping -c1 -w1 ${a}
		log_test_addr ${a} $? 0 "ping in"
	done

	#
	# local traffic
	#
	for a in ${NSA_IP} ${NSA_LO_IP} 127.0.0.1
	do
		log_start
		run_cmd ping -c1 -w1 ${a}
		log_test_addr ${a} $? 0 "ping local"
	done

	#
	# local traffic, socket bound to device
	#
	# address on device
	a=${NSA_IP}
	log_start
	run_cmd ping -c1 -w1 -I ${NSA_DEV} ${a}
	log_test_addr ${a} $? 0 "ping local, device bind"

	# loopback addresses not reachable from device bind
	# fails in a really weird way though because ipv4 special cases
	# route lookups with oif set.
	for a in ${NSA_LO_IP} 127.0.0.1
	do
		log_start
		show_hint "Fails since address on loopback device is out of device scope"
		run_cmd ping -c1 -w1 -I ${NSA_DEV} ${a}
		log_test_addr ${a} $? 1 "ping local, device bind"
	done

	#
	# ip rule blocks reachability to remote address
	#
	log_start
	setup_cmd ip rule add pref 32765 from all lookup local
	setup_cmd ip rule del pref 0 from all lookup local
	setup_cmd ip rule add pref 50 to ${NSB_LO_IP} prohibit
	setup_cmd ip rule add pref 51 from ${NSB_IP} prohibit

	a=${NSB_LO_IP}
	run_cmd ping -c1 -w1 ${a}
	log_test_addr ${a} $? 2 "ping out, blocked by rule"

	# NOTE: ipv4 actually allows the lookup to fail and yet still create
	# a viable rtable if the oif (e.g., bind to device) is set, so this
	# case succeeds despite the rule
	# run_cmd ping -c1 -w1 -I ${NSA_DEV} ${a}

	a=${NSA_LO_IP}
	log_start
	show_hint "Response generates ICMP (or arp request is ignored) due to ip rule"
	run_cmd_nsb ping -c1 -w1 ${a}
	log_test_addr ${a} $? 1 "ping in, blocked by rule"

	[ "$VERBOSE" = "1" ] && echo
	setup_cmd ip rule del pref 32765 from all lookup local
	setup_cmd ip rule add pref 0 from all lookup local
	setup_cmd ip rule del pref 50 to ${NSB_LO_IP} prohibit
	setup_cmd ip rule del pref 51 from ${NSB_IP} prohibit

	#
	# route blocks reachability to remote address
	#
	log_start
	setup_cmd ip route replace unreachable ${NSB_LO_IP}
	setup_cmd ip route replace unreachable ${NSB_IP}

	a=${NSB_LO_IP}
	run_cmd ping -c1 -w1 ${a}
	log_test_addr ${a} $? 2 "ping out, blocked by route"

	# NOTE: ipv4 actually allows the lookup to fail and yet still create
	# a viable rtable if the oif (e.g., bind to device) is set, so this
	# case succeeds despite not having a route for the address
	# run_cmd ping -c1 -w1 -I ${NSA_DEV} ${a}

	a=${NSA_LO_IP}
	log_start
	show_hint "Response is dropped (or arp request is ignored) due to ip route"
	run_cmd_nsb ping -c1 -w1 ${a}
	log_test_addr ${a} $? 1 "ping in, blocked by route"

	#
	# remove 'remote' routes; fallback to default
	#
	log_start
	setup_cmd ip ro del ${NSB_LO_IP}

	a=${NSB_LO_IP}
	run_cmd ping -c1 -w1 ${a}
	log_test_addr ${a} $? 2 "ping out, unreachable default route"

	# NOTE: ipv4 actually allows the lookup to fail and yet still create
	# a viable rtable if the oif (e.g., bind to device) is set, so this
	# case succeeds despite not having a route for the address
	# run_cmd ping -c1 -w1 -I ${NSA_DEV} ${a}
}

ipv4_ping_vrf()
{
	local a

	# should default on; does not exist on older kernels
	set_sysctl net.ipv4.raw_l3mdev_accept=1 2>/dev/null

	#
	# out
	#
	for a in ${NSB_IP} ${NSB_LO_IP}
	do
		log_start
		run_cmd ping -c1 -w1 -I ${VRF} ${a}
		log_test_addr ${a} $? 0 "ping out, VRF bind"

		log_start
		run_cmd ping -c1 -w1 -I ${NSA_DEV} ${a}
		log_test_addr ${a} $? 0 "ping out, device bind"

		log_start
		run_cmd ip vrf exec ${VRF} ping -c1 -w1 -I ${NSA_IP} ${a}
		log_test_addr ${a} $? 0 "ping out, vrf device + dev address bind"

		log_start
		run_cmd ip vrf exec ${VRF} ping -c1 -w1 -I ${VRF_IP} ${a}
		log_test_addr ${a} $? 0 "ping out, vrf device + vrf address bind"
	done

	#
	# in
	#
	for a in ${NSA_IP} ${VRF_IP}
	do
		log_start
		run_cmd_nsb ping -c1 -w1 ${a}
		log_test_addr ${a} $? 0 "ping in"
	done

	#
	# local traffic, local address
	#
	for a in ${NSA_IP} ${VRF_IP} 127.0.0.1
	do
		log_start
		show_hint "Source address should be ${a}"
		run_cmd ping -c1 -w1 -I ${VRF} ${a}
		log_test_addr ${a} $? 0 "ping local, VRF bind"
	done

	#
	# local traffic, socket bound to device
	#
	# address on device
	a=${NSA_IP}
	log_start
	run_cmd ping -c1 -w1 -I ${NSA_DEV} ${a}
	log_test_addr ${a} $? 0 "ping local, device bind"

	# vrf device is out of scope
	for a in ${VRF_IP} 127.0.0.1
	do
		log_start
		show_hint "Fails since address on vrf device is out of device scope"
		run_cmd ping -c1 -w1 -I ${NSA_DEV} ${a}
		log_test_addr ${a} $? 1 "ping local, device bind"
	done

	#
	# ip rule blocks address
	#
	log_start
	setup_cmd ip rule add pref 50 to ${NSB_LO_IP} prohibit
	setup_cmd ip rule add pref 51 from ${NSB_IP} prohibit

	a=${NSB_LO_IP}
	run_cmd ping -c1 -w1 -I ${VRF} ${a}
	log_test_addr ${a} $? 2 "ping out, vrf bind, blocked by rule"

	log_start
	run_cmd ping -c1 -w1 -I ${NSA_DEV} ${a}
	log_test_addr ${a} $? 2 "ping out, device bind, blocked by rule"

	a=${NSA_LO_IP}
	log_start
	show_hint "Response lost due to ip rule"
	run_cmd_nsb ping -c1 -w1 ${a}
	log_test_addr ${a} $? 1 "ping in, blocked by rule"

	[ "$VERBOSE" = "1" ] && echo
	setup_cmd ip rule del pref 50 to ${NSB_LO_IP} prohibit
	setup_cmd ip rule del pref 51 from ${NSB_IP} prohibit

	#
	# remove 'remote' routes; fallback to default
	#
	log_start
	setup_cmd ip ro del vrf ${VRF} ${NSB_LO_IP}

	a=${NSB_LO_IP}
	run_cmd ping -c1 -w1 -I ${VRF} ${a}
	log_test_addr ${a} $? 2 "ping out, vrf bind, unreachable route"

	log_start
	run_cmd ping -c1 -w1 -I ${NSA_DEV} ${a}
	log_test_addr ${a} $? 2 "ping out, device bind, unreachable route"

	a=${NSA_LO_IP}
	log_start
	show_hint "Response lost by unreachable route"
	run_cmd_nsb ping -c1 -w1 ${a}
	log_test_addr ${a} $? 1 "ping in, unreachable route"
}

ipv4_ping()
{
	log_section "IPv4 ping"

	log_subsection "No VRF"
	setup
	set_sysctl net.ipv4.raw_l3mdev_accept=0 2>/dev/null
	ipv4_ping_novrf
	setup
	set_sysctl net.ipv4.raw_l3mdev_accept=1 2>/dev/null
	ipv4_ping_novrf

	log_subsection "With VRF"
	setup "yes"
	ipv4_ping_vrf
}

################################################################################
# IPv4 TCP

#
# MD5 tests without VRF
#
ipv4_tcp_md5_novrf()
{
	#
	# single address
	#

	# basic use case
	log_start
	run_cmd nettest -s -M ${MD5_PW} -m ${NSB_IP} &
	sleep 1
	run_cmd_nsb nettest -r ${NSA_IP} -X ${MD5_PW}
	log_test $? 0 "MD5: Single address config"

	# client sends MD5, server not configured
	log_start
	show_hint "Should timeout due to MD5 mismatch"
	run_cmd nettest -s &
	sleep 1
	run_cmd_nsb nettest -r ${NSA_IP} -X ${MD5_PW}
	log_test $? 2 "MD5: Server no config, client uses password"

	# wrong password
	log_start
	show_hint "Should timeout since client uses wrong password"
	run_cmd nettest -s -M ${MD5_PW} -m ${NSB_IP} &
	sleep 1
	run_cmd_nsb nettest -r ${NSA_IP} -X ${MD5_WRONG_PW}
	log_test $? 2 "MD5: Client uses wrong password"

	# client from different address
	log_start
	show_hint "Should timeout due to MD5 mismatch"
	run_cmd nettest -s -M ${MD5_PW} -m ${NSB_LO_IP} &
	sleep 1
	run_cmd_nsb nettest -r ${NSA_IP} -X ${MD5_PW}
	log_test $? 2 "MD5: Client address does not match address configured with password"

	#
	# MD5 extension - prefix length
	#

	# client in prefix
	log_start
	run_cmd nettest -s -M ${MD5_PW} -m ${NS_NET} &
	sleep 1
	run_cmd_nsb nettest  -r ${NSA_IP} -X ${MD5_PW}
	log_test $? 0 "MD5: Prefix config"

	# client in prefix, wrong password
	log_start
	show_hint "Should timeout since client uses wrong password"
	run_cmd nettest -s -M ${MD5_PW} -m ${NS_NET} &
	sleep 1
	run_cmd_nsb nettest -r ${NSA_IP} -X ${MD5_WRONG_PW}
	log_test $? 2 "MD5: Prefix config, client uses wrong password"

	# client outside of prefix
	log_start
	show_hint "Should timeout due to MD5 mismatch"
	run_cmd nettest -s -M ${MD5_PW} -m ${NS_NET} &
	sleep 1
	run_cmd_nsb nettest -c ${NSB_LO_IP} -r ${NSA_IP} -X ${MD5_PW}
	log_test $? 2 "MD5: Prefix config, client address not in configured prefix"
}

#
# MD5 tests with VRF
#
ipv4_tcp_md5()
{
	#
	# single address
	#

	# basic use case
	log_start
	run_cmd nettest -s -I ${VRF} -M ${MD5_PW} -m ${NSB_IP} &
	sleep 1
	run_cmd_nsb nettest -r ${NSA_IP} -X ${MD5_PW}
	log_test $? 0 "MD5: VRF: Single address config"

	# client sends MD5, server not configured
	log_start
	show_hint "Should timeout since server does not have MD5 auth"
	run_cmd nettest -s -I ${VRF} &
	sleep 1
	run_cmd_nsb nettest -r ${NSA_IP} -X ${MD5_PW}
	log_test $? 2 "MD5: VRF: Server no config, client uses password"

	# wrong password
	log_start
	show_hint "Should timeout since client uses wrong password"
	run_cmd nettest -s -I ${VRF} -M ${MD5_PW} -m ${NSB_IP} &
	sleep 1
	run_cmd_nsb nettest -r ${NSA_IP} -X ${MD5_WRONG_PW}
	log_test $? 2 "MD5: VRF: Client uses wrong password"

	# client from different address
	log_start
	show_hint "Should timeout since server config differs from client"
	run_cmd nettest -s -I ${VRF} -M ${MD5_PW} -m ${NSB_LO_IP} &
	sleep 1
	run_cmd_nsb nettest -r ${NSA_IP} -X ${MD5_PW}
	log_test $? 2 "MD5: VRF: Client address does not match address configured with password"

	#
	# MD5 extension - prefix length
	#

	# client in prefix
	log_start
	run_cmd nettest -s -I ${VRF} -M ${MD5_PW} -m ${NS_NET} &
	sleep 1
	run_cmd_nsb nettest  -r ${NSA_IP} -X ${MD5_PW}
	log_test $? 0 "MD5: VRF: Prefix config"

	# client in prefix, wrong password
	log_start
	show_hint "Should timeout since client uses wrong password"
	run_cmd nettest -s -I ${VRF} -M ${MD5_PW} -m ${NS_NET} &
	sleep 1
	run_cmd_nsb nettest -r ${NSA_IP} -X ${MD5_WRONG_PW}
	log_test $? 2 "MD5: VRF: Prefix config, client uses wrong password"

	# client outside of prefix
	log_start
	show_hint "Should timeout since client address is outside of prefix"
	run_cmd nettest -s -I ${VRF} -M ${MD5_PW} -m ${NS_NET} &
	sleep 1
	run_cmd_nsb nettest -c ${NSB_LO_IP} -r ${NSA_IP} -X ${MD5_PW}
	log_test $? 2 "MD5: VRF: Prefix config, client address not in configured prefix"

	#
	# duplicate config between default VRF and a VRF
	#

	log_start
	run_cmd nettest -s -I ${VRF} -M ${MD5_PW} -m ${NSB_IP} &
	run_cmd nettest -s -M ${MD5_WRONG_PW} -m ${NSB_IP} &
	sleep 1
	run_cmd_nsb nettest  -r ${NSA_IP} -X ${MD5_PW}
	log_test $? 0 "MD5: VRF: Single address config in default VRF and VRF, conn in VRF"

	log_start
	run_cmd nettest -s -I ${VRF} -M ${MD5_PW} -m ${NSB_IP} &
	run_cmd nettest -s -M ${MD5_WRONG_PW} -m ${NSB_IP} &
	sleep 1
	run_cmd_nsc nettest  -r ${NSA_IP} -X ${MD5_WRONG_PW}
	log_test $? 0 "MD5: VRF: Single address config in default VRF and VRF, conn in default VRF"

	log_start
	show_hint "Should timeout since client in default VRF uses VRF password"
	run_cmd nettest -s -I ${VRF} -M ${MD5_PW} -m ${NSB_IP} &
	run_cmd nettest -s -M ${MD5_WRONG_PW} -m ${NSB_IP} &
	sleep 1
	run_cmd_nsc nettest -r ${NSA_IP} -X ${MD5_PW}
	log_test $? 2 "MD5: VRF: Single address config in default VRF and VRF, conn in default VRF with VRF pw"

	log_start
	show_hint "Should timeout since client in VRF uses default VRF password"
	run_cmd nettest -s -I ${VRF} -M ${MD5_PW} -m ${NSB_IP} &
	run_cmd nettest -s -M ${MD5_WRONG_PW} -m ${NSB_IP} &
	sleep 1
	run_cmd_nsb nettest -r ${NSA_IP} -X ${MD5_WRONG_PW}
	log_test $? 2 "MD5: VRF: Single address config in default VRF and VRF, conn in VRF with default VRF pw"

	log_start
	run_cmd nettest -s -I ${VRF} -M ${MD5_PW} -m ${NS_NET} &
	run_cmd nettest -s -M ${MD5_WRONG_PW} -m ${NS_NET} &
	sleep 1
	run_cmd_nsb nettest  -r ${NSA_IP} -X ${MD5_PW}
	log_test $? 0 "MD5: VRF: Prefix config in default VRF and VRF, conn in VRF"

	log_start
	run_cmd nettest -s -I ${VRF} -M ${MD5_PW} -m ${NS_NET} &
	run_cmd nettest -s -M ${MD5_WRONG_PW} -m ${NS_NET} &
	sleep 1
	run_cmd_nsc nettest  -r ${NSA_IP} -X ${MD5_WRONG_PW}
	log_test $? 0 "MD5: VRF: Prefix config in default VRF and VRF, conn in default VRF"

	log_start
	show_hint "Should timeout since client in default VRF uses VRF password"
	run_cmd nettest -s -I ${VRF} -M ${MD5_PW} -m ${NS_NET} &
	run_cmd nettest -s -M ${MD5_WRONG_PW} -m ${NS_NET} &
	sleep 1
	run_cmd_nsc nettest -r ${NSA_IP} -X ${MD5_PW}
	log_test $? 2 "MD5: VRF: Prefix config in default VRF and VRF, conn in default VRF with VRF pw"

	log_start
	show_hint "Should timeout since client in VRF uses default VRF password"
	run_cmd nettest -s -I ${VRF} -M ${MD5_PW} -m ${NS_NET} &
	run_cmd nettest -s -M ${MD5_WRONG_PW} -m ${NS_NET} &
	sleep 1
	run_cmd_nsb nettest -r ${NSA_IP} -X ${MD5_WRONG_PW}
	log_test $? 2 "MD5: VRF: Prefix config in default VRF and VRF, conn in VRF with default VRF pw"

	#
	# negative tests
	#
	log_start
	run_cmd nettest -s -I ${NSA_DEV} -M ${MD5_PW} -m ${NSB_IP}
	log_test $? 1 "MD5: VRF: Device must be a VRF - single address"

	log_start
	run_cmd nettest -s -I ${NSA_DEV} -M ${MD5_PW} -m ${NS_NET}
	log_test $? 1 "MD5: VRF: Device must be a VRF - prefix"

	test_ipv4_md5_vrf__vrf_server__no_bind_ifindex
	test_ipv4_md5_vrf__global_server__bind_ifindex0
}

test_ipv4_md5_vrf__vrf_server__no_bind_ifindex()
{
	log_start
	show_hint "Simulates applications using VRF without TCP_MD5SIG_FLAG_IFINDEX"
	run_cmd nettest -s -I ${VRF} -M ${MD5_PW} -m ${NS_NET} --no-bind-key-ifindex &
	sleep 1
	run_cmd_nsb nettest -r ${NSA_IP} -X ${MD5_PW}
	log_test $? 0 "MD5: VRF: VRF-bound server, unbound key accepts connection"

	log_start
	show_hint "Binding both the socket and the key is not required but it works"
	run_cmd nettest -s -I ${VRF} -M ${MD5_PW} -m ${NS_NET} --force-bind-key-ifindex &
	sleep 1
	run_cmd_nsb nettest -r ${NSA_IP} -X ${MD5_PW}
	log_test $? 0 "MD5: VRF: VRF-bound server, bound key accepts connection"
}

test_ipv4_md5_vrf__global_server__bind_ifindex0()
{
	# This particular test needs tcp_l3mdev_accept=1 for Global server to accept VRF connections
	local old_tcp_l3mdev_accept
	old_tcp_l3mdev_accept=$(get_sysctl net.ipv4.tcp_l3mdev_accept)
	set_sysctl net.ipv4.tcp_l3mdev_accept=1

	log_start
	run_cmd nettest -s -M ${MD5_PW} -m ${NS_NET} --force-bind-key-ifindex &
	sleep 1
	run_cmd_nsb nettest -r ${NSA_IP} -X ${MD5_PW}
	log_test $? 2 "MD5: VRF: Global server, Key bound to ifindex=0 rejects VRF connection"

	log_start
	run_cmd nettest -s -M ${MD5_PW} -m ${NS_NET} --force-bind-key-ifindex &
	sleep 1
	run_cmd_nsc nettest -r ${NSA_IP} -X ${MD5_PW}
	log_test $? 0 "MD5: VRF: Global server, key bound to ifindex=0 accepts non-VRF connection"
	log_start

	run_cmd nettest -s -M ${MD5_PW} -m ${NS_NET} --no-bind-key-ifindex &
	sleep 1
	run_cmd_nsb nettest -r ${NSA_IP} -X ${MD5_PW}
	log_test $? 0 "MD5: VRF: Global server, key not bound to ifindex accepts VRF connection"

	log_start
	run_cmd nettest -s -M ${MD5_PW} -m ${NS_NET} --no-bind-key-ifindex &
	sleep 1
	run_cmd_nsc nettest -r ${NSA_IP} -X ${MD5_PW}
	log_test $? 0 "MD5: VRF: Global server, key not bound to ifindex accepts non-VRF connection"

	# restore value
	set_sysctl net.ipv4.tcp_l3mdev_accept="$old_tcp_l3mdev_accept"
}

ipv4_tcp_novrf()
{
	local a

	#
	# server tests
	#
	for a in ${NSA_IP} ${NSA_LO_IP}
	do
		log_start
		run_cmd nettest -s &
		sleep 1
		run_cmd_nsb nettest -r ${a}
		log_test_addr ${a} $? 0 "Global server"
	done

	a=${NSA_IP}
	log_start
	run_cmd nettest -s -I ${NSA_DEV} &
	sleep 1
	run_cmd_nsb nettest -r ${a}
	log_test_addr ${a} $? 0 "Device server"

	# verify TCP reset sent and received
	for a in ${NSA_IP} ${NSA_LO_IP}
	do
		log_start
		show_hint "Should fail 'Connection refused' since there is no server"
		run_cmd_nsb nettest -r ${a}
		log_test_addr ${a} $? 1 "No server"
	done

	#
	# client
	#
	for a in ${NSB_IP} ${NSB_LO_IP}
	do
		log_start
		run_cmd_nsb nettest -s &
		sleep 1
		run_cmd nettest -r ${a} -0 ${NSA_IP}
		log_test_addr ${a} $? 0 "Client"

		log_start
		run_cmd_nsb nettest -s &
		sleep 1
		run_cmd nettest -r ${a} -d ${NSA_DEV}
		log_test_addr ${a} $? 0 "Client, device bind"

		log_start
		show_hint "Should fail 'Connection refused'"
		run_cmd nettest -r ${a}
		log_test_addr ${a} $? 1 "No server, unbound client"

		log_start
		show_hint "Should fail 'Connection refused'"
		run_cmd nettest -r ${a} -d ${NSA_DEV}
		log_test_addr ${a} $? 1 "No server, device client"
	done

	#
	# local address tests
	#
	for a in ${NSA_IP} ${NSA_LO_IP} 127.0.0.1
	do
		log_start
		run_cmd nettest -s &
		sleep 1
		run_cmd nettest -r ${a} -0 ${a} -1 ${a}
		log_test_addr ${a} $? 0 "Global server, local connection"
	done

	a=${NSA_IP}
	log_start
	run_cmd nettest -s -I ${NSA_DEV} &
	sleep 1
	run_cmd nettest -r ${a} -0 ${a}
	log_test_addr ${a} $? 0 "Device server, unbound client, local connection"

	for a in ${NSA_LO_IP} 127.0.0.1
	do
		log_start
		show_hint "Should fail 'Connection refused' since addresses on loopback are out of device scope"
		run_cmd nettest -s -I ${NSA_DEV} &
		sleep 1
		run_cmd nettest -r ${a}
		log_test_addr ${a} $? 1 "Device server, unbound client, local connection"
	done

	a=${NSA_IP}
	log_start
	run_cmd nettest -s &
	sleep 1
	run_cmd nettest -r ${a} -0 ${a} -d ${NSA_DEV}
	log_test_addr ${a} $? 0 "Global server, device client, local connection"

	for a in ${NSA_LO_IP} 127.0.0.1
	do
		log_start
		show_hint "Should fail 'No route to host' since addresses on loopback are out of device scope"
		run_cmd nettest -s &
		sleep 1
		run_cmd nettest -r ${a} -d ${NSA_DEV}
		log_test_addr ${a} $? 1 "Global server, device client, local connection"
	done

	a=${NSA_IP}
	log_start
	run_cmd nettest -s -I ${NSA_DEV} -3 ${NSA_DEV} &
	sleep 1
	run_cmd nettest  -d ${NSA_DEV} -r ${a} -0 ${a}
	log_test_addr ${a} $? 0 "Device server, device client, local connection"

	log_start
	show_hint "Should fail 'Connection refused'"
	run_cmd nettest -d ${NSA_DEV} -r ${a}
	log_test_addr ${a} $? 1 "No server, device client, local conn"

	ipv4_tcp_md5_novrf
}

ipv4_tcp_vrf()
{
	local a

	# disable global server
	log_subsection "Global server disabled"

	set_sysctl net.ipv4.tcp_l3mdev_accept=0

	#
	# server tests
	#
	for a in ${NSA_IP} ${VRF_IP}
	do
		log_start
		show_hint "Should fail 'Connection refused' since global server with VRF is disabled"
		run_cmd nettest -s &
		sleep 1
		run_cmd_nsb nettest -r ${a}
		log_test_addr ${a} $? 1 "Global server"

		log_start
		run_cmd nettest -s -I ${VRF} -3 ${VRF} &
		sleep 1
		run_cmd_nsb nettest -r ${a}
		log_test_addr ${a} $? 0 "VRF server"

		log_start
		run_cmd nettest -s -I ${NSA_DEV} -3 ${NSA_DEV} &
		sleep 1
		run_cmd_nsb nettest -r ${a}
		log_test_addr ${a} $? 0 "Device server"

		# verify TCP reset received
		log_start
		show_hint "Should fail 'Connection refused' since there is no server"
		run_cmd_nsb nettest -r ${a}
		log_test_addr ${a} $? 1 "No server"
	done

	# local address tests
	# (${VRF_IP} and 127.0.0.1 both timeout)
	a=${NSA_IP}
	log_start
	show_hint "Should fail 'Connection refused' since global server with VRF is disabled"
	run_cmd nettest -s &
	sleep 1
	run_cmd nettest -r ${a} -d ${NSA_DEV}
	log_test_addr ${a} $? 1 "Global server, local connection"

	# run MD5 tests
	setup_vrf_dup
	ipv4_tcp_md5
	cleanup_vrf_dup

	#
	# enable VRF global server
	#
	log_subsection "VRF Global server enabled"
	set_sysctl net.ipv4.tcp_l3mdev_accept=1

	for a in ${NSA_IP} ${VRF_IP}
	do
		log_start
		show_hint "client socket should be bound to VRF"
		run_cmd nettest -s -3 ${VRF} &
		sleep 1
		run_cmd_nsb nettest -r ${a}
		log_test_addr ${a} $? 0 "Global server"

		log_start
		show_hint "client socket should be bound to VRF"
		run_cmd nettest -s -I ${VRF} -3 ${VRF} &
		sleep 1
		run_cmd_nsb nettest -r ${a}
		log_test_addr ${a} $? 0 "VRF server"

		# verify TCP reset received
		log_start
		show_hint "Should fail 'Connection refused'"
		run_cmd_nsb nettest -r ${a}
		log_test_addr ${a} $? 1 "No server"
	done

	a=${NSA_IP}
	log_start
	show_hint "client socket should be bound to device"
	run_cmd nettest -s -I ${NSA_DEV} -3 ${NSA_DEV} &
	sleep 1
	run_cmd_nsb nettest -r ${a}
	log_test_addr ${a} $? 0 "Device server"

	# local address tests
	for a in ${NSA_IP} ${VRF_IP}
	do
		log_start
		show_hint "Should fail 'Connection refused' since client is not bound to VRF"
		run_cmd nettest -s -I ${VRF} &
		sleep 1
		run_cmd nettest -r ${a}
		log_test_addr ${a} $? 1 "Global server, local connection"
	done

	#
	# client
	#
	for a in ${NSB_IP} ${NSB_LO_IP}
	do
		log_start
		run_cmd_nsb nettest -s &
		sleep 1
		run_cmd nettest -r ${a} -d ${VRF}
		log_test_addr ${a} $? 0 "Client, VRF bind"

		log_start
		run_cmd_nsb nettest -s &
		sleep 1
		run_cmd nettest -r ${a} -d ${NSA_DEV}
		log_test_addr ${a} $? 0 "Client, device bind"

		log_start
		show_hint "Should fail 'Connection refused'"
		run_cmd nettest -r ${a} -d ${VRF}
		log_test_addr ${a} $? 1 "No server, VRF client"

		log_start
		show_hint "Should fail 'Connection refused'"
		run_cmd nettest -r ${a} -d ${NSA_DEV}
		log_test_addr ${a} $? 1 "No server, device client"
	done

	for a in ${NSA_IP} ${VRF_IP} 127.0.0.1
	do
		log_start
		run_cmd nettest -s -I ${VRF} -3 ${VRF} &
		sleep 1
		run_cmd nettest -r ${a} -d ${VRF} -0 ${a}
		log_test_addr ${a} $? 0 "VRF server, VRF client, local connection"
	done

	a=${NSA_IP}
	log_start
	run_cmd nettest -s -I ${VRF} -3 ${VRF} &
	sleep 1
	run_cmd nettest -r ${a} -d ${NSA_DEV} -0 ${a}
	log_test_addr ${a} $? 0 "VRF server, device client, local connection"

	log_start
	show_hint "Should fail 'No route to host' since client is out of VRF scope"
	run_cmd nettest -s -I ${VRF} &
	sleep 1
	run_cmd nettest -r ${a}
	log_test_addr ${a} $? 1 "VRF server, unbound client, local connection"

	log_start
	run_cmd nettest -s -I ${NSA_DEV} -3 ${NSA_DEV} &
	sleep 1
	run_cmd nettest -r ${a} -d ${VRF} -0 ${a}
	log_test_addr ${a} $? 0 "Device server, VRF client, local connection"

	log_start
	run_cmd nettest -s -I ${NSA_DEV} -3 ${NSA_DEV} &
	sleep 1
	run_cmd nettest -r ${a} -d ${NSA_DEV} -0 ${a}
	log_test_addr ${a} $? 0 "Device server, device client, local connection"
}

ipv4_tcp()
{
	log_section "IPv4/TCP"
	log_subsection "No VRF"
	setup

	# tcp_l3mdev_accept should have no affect without VRF;
	# run tests with it enabled and disabled to verify
	log_subsection "tcp_l3mdev_accept disabled"
	set_sysctl net.ipv4.tcp_l3mdev_accept=0
	ipv4_tcp_novrf
	log_subsection "tcp_l3mdev_accept enabled"
	set_sysctl net.ipv4.tcp_l3mdev_accept=1
	ipv4_tcp_novrf

	log_subsection "With VRF"
	setup "yes"
	ipv4_tcp_vrf
}

################################################################################
# IPv4 UDP

ipv4_udp_novrf()
{
	local a

	#
	# server tests
	#
	for a in ${NSA_IP} ${NSA_LO_IP}
	do
		log_start
		run_cmd nettest -D -s -3 ${NSA_DEV} &
		sleep 1
		run_cmd_nsb nettest -D -r ${a}
		log_test_addr ${a} $? 0 "Global server"

		log_start
		show_hint "Should fail 'Connection refused' since there is no server"
		run_cmd_nsb nettest -D -r ${a}
		log_test_addr ${a} $? 1 "No server"
	done

	a=${NSA_IP}
	log_start
	run_cmd nettest -D -I ${NSA_DEV} -s -3 ${NSA_DEV} &
	sleep 1
	run_cmd_nsb nettest -D -r ${a}
	log_test_addr ${a} $? 0 "Device server"

	#
	# client
	#
	for a in ${NSB_IP} ${NSB_LO_IP}
	do
		log_start
		run_cmd_nsb nettest -D -s &
		sleep 1
		run_cmd nettest -D -r ${a} -0 ${NSA_IP}
		log_test_addr ${a} $? 0 "Client"

		log_start
		run_cmd_nsb nettest -D -s &
		sleep 1
		run_cmd nettest -D -r ${a} -d ${NSA_DEV} -0 ${NSA_IP}
		log_test_addr ${a} $? 0 "Client, device bind"

		log_start
		run_cmd_nsb nettest -D -s &
		sleep 1
		run_cmd nettest -D -r ${a} -d ${NSA_DEV} -C -0 ${NSA_IP}
		log_test_addr ${a} $? 0 "Client, device send via cmsg"

		log_start
		run_cmd_nsb nettest -D -s &
		sleep 1
		run_cmd nettest -D -r ${a} -d ${NSA_DEV} -S -0 ${NSA_IP}
		log_test_addr ${a} $? 0 "Client, device bind via IP_UNICAST_IF"

		log_start
		show_hint "Should fail 'Connection refused'"
		run_cmd nettest -D -r ${a}
		log_test_addr ${a} $? 1 "No server, unbound client"

		log_start
		show_hint "Should fail 'Connection refused'"
		run_cmd nettest -D -r ${a} -d ${NSA_DEV}
		log_test_addr ${a} $? 1 "No server, device client"
	done

	#
	# local address tests
	#
	for a in ${NSA_IP} ${NSA_LO_IP} 127.0.0.1
	do
		log_start
		run_cmd nettest -D -s &
		sleep 1
		run_cmd nettest -D -r ${a} -0 ${a} -1 ${a}
		log_test_addr ${a} $? 0 "Global server, local connection"
	done

	a=${NSA_IP}
	log_start
	run_cmd nettest -s -D -I ${NSA_DEV} -3 ${NSA_DEV} &
	sleep 1
	run_cmd nettest -D -r ${a}
	log_test_addr ${a} $? 0 "Device server, unbound client, local connection"

	for a in ${NSA_LO_IP} 127.0.0.1
	do
		log_start
		show_hint "Should fail 'Connection refused' since address is out of device scope"
		run_cmd nettest -s -D -I ${NSA_DEV} &
		sleep 1
		run_cmd nettest -D -r ${a}
		log_test_addr ${a} $? 1 "Device server, unbound client, local connection"
	done

	a=${NSA_IP}
	log_start
	run_cmd nettest -s -D &
	sleep 1
	run_cmd nettest -D -d ${NSA_DEV} -r ${a}
	log_test_addr ${a} $? 0 "Global server, device client, local connection"

	log_start
	run_cmd nettest -s -D &
	sleep 1
	run_cmd nettest -D -d ${NSA_DEV} -C -r ${a}
	log_test_addr ${a} $? 0 "Global server, device send via cmsg, local connection"

	log_start
	run_cmd nettest -s -D &
	sleep 1
	run_cmd nettest -D -d ${NSA_DEV} -S -r ${a}
	log_test_addr ${a} $? 0 "Global server, device client via IP_UNICAST_IF, local connection"

	# IPv4 with device bind has really weird behavior - it overrides the
	# fib lookup, generates an rtable and tries to send the packet. This
	# causes failures for local traffic at different places
	for a in ${NSA_LO_IP} 127.0.0.1
	do
		log_start
		show_hint "Should fail since addresses on loopback are out of device scope"
		run_cmd nettest -D -s &
		sleep 1
		run_cmd nettest -D -r ${a} -d ${NSA_DEV}
		log_test_addr ${a} $? 2 "Global server, device client, local connection"

		log_start
		show_hint "Should fail since addresses on loopback are out of device scope"
		run_cmd nettest -D -s &
		sleep 1
		run_cmd nettest -D -r ${a} -d ${NSA_DEV} -C
		log_test_addr ${a} $? 1 "Global server, device send via cmsg, local connection"

		log_start
		show_hint "Should fail since addresses on loopback are out of device scope"
		run_cmd nettest -D -s &
		sleep 1
		run_cmd nettest -D -r ${a} -d ${NSA_DEV} -S
		log_test_addr ${a} $? 1 "Global server, device client via IP_UNICAST_IF, local connection"
	done

	a=${NSA_IP}
	log_start
	run_cmd nettest -D -s -I ${NSA_DEV} -3 ${NSA_DEV} &
	sleep 1
	run_cmd nettest -D -d ${NSA_DEV} -r ${a} -0 ${a}
	log_test_addr ${a} $? 0 "Device server, device client, local conn"

	log_start
	run_cmd nettest -D -d ${NSA_DEV} -r ${a}
	log_test_addr ${a} $? 2 "No server, device client, local conn"
}

ipv4_udp_vrf()
{
	local a

	# disable global server
	log_subsection "Global server disabled"
	set_sysctl net.ipv4.udp_l3mdev_accept=0

	#
	# server tests
	#
	for a in ${NSA_IP} ${VRF_IP}
	do
		log_start
		show_hint "Fails because ingress is in a VRF and global server is disabled"
		run_cmd nettest -D -s &
		sleep 1
		run_cmd_nsb nettest -D -r ${a}
		log_test_addr ${a} $? 1 "Global server"

		log_start
		run_cmd nettest -D -I ${VRF} -s -3 ${NSA_DEV} &
		sleep 1
		run_cmd_nsb nettest -D -r ${a}
		log_test_addr ${a} $? 0 "VRF server"

		log_start
		run_cmd nettest -D -I ${NSA_DEV} -s -3 ${NSA_DEV} &
		sleep 1
		run_cmd_nsb nettest -D -r ${a}
		log_test_addr ${a} $? 0 "Enslaved device server"

		log_start
		show_hint "Should fail 'Connection refused' since there is no server"
		run_cmd_nsb nettest -D -r ${a}
		log_test_addr ${a} $? 1 "No server"

		log_start
		show_hint "Should fail 'Connection refused' since global server is out of scope"
		run_cmd nettest -D -s &
		sleep 1
		run_cmd nettest -D -d ${VRF} -r ${a}
		log_test_addr ${a} $? 1 "Global server, VRF client, local connection"
	done

	a=${NSA_IP}
	log_start
	run_cmd nettest -s -D -I ${VRF} -3 ${NSA_DEV} &
	sleep 1
	run_cmd nettest -D -d ${VRF} -r ${a}
	log_test_addr ${a} $? 0 "VRF server, VRF client, local conn"

	log_start
	run_cmd nettest -s -D -I ${VRF} -3 ${NSA_DEV} &
	sleep 1
	run_cmd nettest -D -d ${NSA_DEV} -r ${a}
	log_test_addr ${a} $? 0 "VRF server, enslaved device client, local connection"

	a=${NSA_IP}
	log_start
	run_cmd nettest -s -D -I ${NSA_DEV} -3 ${NSA_DEV} &
	sleep 1
	run_cmd nettest -D -d ${VRF} -r ${a}
	log_test_addr ${a} $? 0 "Enslaved device server, VRF client, local conn"

	log_start
	run_cmd nettest -s -D -I ${NSA_DEV} -3 ${NSA_DEV} &
	sleep 1
	run_cmd nettest -D -d ${NSA_DEV} -r ${a}
	log_test_addr ${a} $? 0 "Enslaved device server, device client, local conn"

	# enable global server
	log_subsection "Global server enabled"
	set_sysctl net.ipv4.udp_l3mdev_accept=1

	#
	# server tests
	#
	for a in ${NSA_IP} ${VRF_IP}
	do
		log_start
		run_cmd nettest -D -s -3 ${NSA_DEV} &
		sleep 1
		run_cmd_nsb nettest -D -r ${a}
		log_test_addr ${a} $? 0 "Global server"

		log_start
		run_cmd nettest -D -I ${VRF} -s -3 ${NSA_DEV} &
		sleep 1
		run_cmd_nsb nettest -D -r ${a}
		log_test_addr ${a} $? 0 "VRF server"

		log_start
		run_cmd nettest -D -I ${NSA_DEV} -s -3 ${NSA_DEV} &
		sleep 1
		run_cmd_nsb nettest -D -r ${a}
		log_test_addr ${a} $? 0 "Enslaved device server"

		log_start
		show_hint "Should fail 'Connection refused'"
		run_cmd_nsb nettest -D -r ${a}
		log_test_addr ${a} $? 1 "No server"
	done

	#
	# client tests
	#
	log_start
	run_cmd_nsb nettest -D -s &
	sleep 1
	run_cmd nettest -d ${VRF} -D -r ${NSB_IP} -1 ${NSA_IP}
	log_test $? 0 "VRF client"

	log_start
	run_cmd_nsb nettest -D -s &
	sleep 1
	run_cmd nettest -d ${NSA_DEV} -D -r ${NSB_IP} -1 ${NSA_IP}
	log_test $? 0 "Enslaved device client"

	# negative test - should fail
	log_start
	show_hint "Should fail 'Connection refused'"
	run_cmd nettest -D -d ${VRF} -r ${NSB_IP}
	log_test $? 1 "No server, VRF client"

	log_start
	show_hint "Should fail 'Connection refused'"
	run_cmd nettest -D -d ${NSA_DEV} -r ${NSB_IP}
	log_test $? 1 "No server, enslaved device client"

	#
	# local address tests
	#
	a=${NSA_IP}
	log_start
	run_cmd nettest -D -s -3 ${NSA_DEV} &
	sleep 1
	run_cmd nettest -D -d ${VRF} -r ${a}
	log_test_addr ${a} $? 0 "Global server, VRF client, local conn"

	log_start
	run_cmd nettest -s -D -I ${VRF} -3 ${NSA_DEV} &
	sleep 1
	run_cmd nettest -D -d ${VRF} -r ${a}
	log_test_addr ${a} $? 0 "VRF server, VRF client, local conn"

	log_start
	run_cmd nettest -s -D -I ${VRF} -3 ${NSA_DEV} &
	sleep 1
	run_cmd nettest -D -d ${NSA_DEV} -r ${a}
	log_test_addr ${a} $? 0 "VRF server, device client, local conn"

	log_start
	run_cmd nettest -s -D -I ${NSA_DEV} -3 ${NSA_DEV} &
	sleep 1
	run_cmd nettest -D -d ${VRF} -r ${a}
	log_test_addr ${a} $? 0 "Enslaved device server, VRF client, local conn"

	log_start
	run_cmd nettest -s -D -I ${NSA_DEV} -3 ${NSA_DEV} &
	sleep 1
	run_cmd nettest -D -d ${NSA_DEV} -r ${a}
	log_test_addr ${a} $? 0 "Enslaved device server, device client, local conn"

	for a in ${VRF_IP} 127.0.0.1
	do
		log_start
		run_cmd nettest -D -s -3 ${VRF} &
		sleep 1
		run_cmd nettest -D -d ${VRF} -r ${a}
		log_test_addr ${a} $? 0 "Global server, VRF client, local conn"
	done

	for a in ${VRF_IP} 127.0.0.1
	do
		log_start
		run_cmd nettest -s -D -I ${VRF} -3 ${VRF} &
		sleep 1
		run_cmd nettest -D -d ${VRF} -r ${a}
		log_test_addr ${a} $? 0 "VRF server, VRF client, local conn"
	done

	# negative test - should fail
	# verifies ECONNREFUSED
	for a in ${NSA_IP} ${VRF_IP} 127.0.0.1
	do
		log_start
		show_hint "Should fail 'Connection refused'"
		run_cmd nettest -D -d ${VRF} -r ${a}
		log_test_addr ${a} $? 1 "No server, VRF client, local conn"
	done
}

ipv4_udp()
{
	log_section "IPv4/UDP"
	log_subsection "No VRF"

	setup

	# udp_l3mdev_accept should have no affect without VRF;
	# run tests with it enabled and disabled to verify
	log_subsection "udp_l3mdev_accept disabled"
	set_sysctl net.ipv4.udp_l3mdev_accept=0
	ipv4_udp_novrf
	log_subsection "udp_l3mdev_accept enabled"
	set_sysctl net.ipv4.udp_l3mdev_accept=1
	ipv4_udp_novrf

	log_subsection "With VRF"
	setup "yes"
	ipv4_udp_vrf
}

################################################################################
# IPv4 address bind
#
# verifies ability or inability to bind to an address / device

ipv4_addr_bind_novrf()
{
	#
	# raw socket
	#
	for a in ${NSA_IP} ${NSA_LO_IP}
	do
		log_start
		run_cmd nettest -s -R -P icmp -l ${a} -b
		log_test_addr ${a} $? 0 "Raw socket bind to local address"

		log_start
		run_cmd nettest -s -R -P icmp -l ${a} -I ${NSA_DEV} -b
		log_test_addr ${a} $? 0 "Raw socket bind to local address after device bind"
	done

	#
	# tcp sockets
	#
	a=${NSA_IP}
	log_start
	run_cmd nettest -c ${a} -r ${NSB_IP} -t1 -b
	log_test_addr ${a} $? 0 "TCP socket bind to local address"

	log_start
	run_cmd nettest -c ${a} -r ${NSB_IP} -d ${NSA_DEV} -t1 -b
	log_test_addr ${a} $? 0 "TCP socket bind to local address after device bind"

	# Sadly, the kernel allows binding a socket to a device and then
	# binding to an address not on the device. The only restriction
	# is that the address is valid in the L3 domain. So this test
	# passes when it really should not
	#a=${NSA_LO_IP}
	#log_start
	#show_hint "Should fail with 'Cannot assign requested address'"
	#run_cmd nettest -s -l ${a} -I ${NSA_DEV} -t1 -b
	#log_test_addr ${a} $? 1 "TCP socket bind to out of scope local address"
}

ipv4_addr_bind_vrf()
{
	#
	# raw socket
	#
	for a in ${NSA_IP} ${VRF_IP}
	do
		log_start
		show_hint "Socket not bound to VRF, but address is in VRF"
		run_cmd nettest -s -R -P icmp -l ${a} -b
		log_test_addr ${a} $? 1 "Raw socket bind to local address"

		log_start
		run_cmd nettest -s -R -P icmp -l ${a} -I ${NSA_DEV} -b
		log_test_addr ${a} $? 0 "Raw socket bind to local address after device bind"
		log_start
		run_cmd nettest -s -R -P icmp -l ${a} -I ${VRF} -b
		log_test_addr ${a} $? 0 "Raw socket bind to local address after VRF bind"
	done

	a=${NSA_LO_IP}
	log_start
	show_hint "Address on loopback is out of VRF scope"
	run_cmd nettest -s -R -P icmp -l ${a} -I ${VRF} -b
	log_test_addr ${a} $? 1 "Raw socket bind to out of scope address after VRF bind"

	#
	# tcp sockets
	#
	for a in ${NSA_IP} ${VRF_IP}
	do
		log_start
		run_cmd nettest -s -l ${a} -I ${VRF} -t1 -b
		log_test_addr ${a} $? 0 "TCP socket bind to local address"

		log_start
		run_cmd nettest -s -l ${a} -I ${NSA_DEV} -t1 -b
		log_test_addr ${a} $? 0 "TCP socket bind to local address after device bind"
	done

	a=${NSA_LO_IP}
	log_start
	show_hint "Address on loopback out of scope for VRF"
	run_cmd nettest -s -l ${a} -I ${VRF} -t1 -b
	log_test_addr ${a} $? 1 "TCP socket bind to invalid local address for VRF"

	log_start
	show_hint "Address on loopback out of scope for device in VRF"
	run_cmd nettest -s -l ${a} -I ${NSA_DEV} -t1 -b
	log_test_addr ${a} $? 1 "TCP socket bind to invalid local address for device bind"
}

ipv4_addr_bind()
{
	log_section "IPv4 address binds"

	log_subsection "No VRF"
	setup
	ipv4_addr_bind_novrf

	log_subsection "With VRF"
	setup "yes"
	ipv4_addr_bind_vrf
}

################################################################################
# IPv4 runtime tests

ipv4_rt()
{
	local desc="$1"
	local varg="$2"
	local with_vrf="yes"
	local a

	#
	# server tests
	#
	for a in ${NSA_IP} ${VRF_IP}
	do
		log_start
		run_cmd nettest ${varg} -s &
		sleep 1
		run_cmd_nsb nettest ${varg} -r ${a} &
		sleep 3
		run_cmd ip link del ${VRF}
		sleep 1
		log_test_addr ${a} 0 0 "${desc}, global server"

		setup ${with_vrf}
	done

	for a in ${NSA_IP} ${VRF_IP}
	do
		log_start
		run_cmd nettest ${varg} -s -I ${VRF} &
		sleep 1
		run_cmd_nsb nettest ${varg} -r ${a} &
		sleep 3
		run_cmd ip link del ${VRF}
		sleep 1
		log_test_addr ${a} 0 0 "${desc}, VRF server"

		setup ${with_vrf}
	done

	a=${NSA_IP}
	log_start
	run_cmd nettest ${varg} -s -I ${NSA_DEV} &
	sleep 1
	run_cmd_nsb nettest ${varg} -r ${a} &
	sleep 3
	run_cmd ip link del ${VRF}
	sleep 1
	log_test_addr ${a} 0 0 "${desc}, enslaved device server"

	setup ${with_vrf}

	#
	# client test
	#
	log_start
	run_cmd_nsb nettest ${varg} -s &
	sleep 1
	run_cmd nettest ${varg} -d ${VRF} -r ${NSB_IP} &
	sleep 3
	run_cmd ip link del ${VRF}
	sleep 1
	log_test_addr ${a} 0 0 "${desc}, VRF client"

	setup ${with_vrf}

	log_start
	run_cmd_nsb nettest ${varg} -s &
	sleep 1
	run_cmd nettest ${varg} -d ${NSA_DEV} -r ${NSB_IP} &
	sleep 3
	run_cmd ip link del ${VRF}
	sleep 1
	log_test_addr ${a} 0 0 "${desc}, enslaved device client"

	setup ${with_vrf}

	#
	# local address tests
	#
	for a in ${NSA_IP} ${VRF_IP}
	do
		log_start
		run_cmd nettest ${varg} -s &
		sleep 1
		run_cmd nettest ${varg} -d ${VRF} -r ${a} &
		sleep 3
		run_cmd ip link del ${VRF}
		sleep 1
		log_test_addr ${a} 0 0 "${desc}, global server, VRF client, local"

		setup ${with_vrf}
	done

	for a in ${NSA_IP} ${VRF_IP}
	do
		log_start
		run_cmd nettest ${varg} -I ${VRF} -s &
		sleep 1
		run_cmd nettest ${varg} -d ${VRF} -r ${a} &
		sleep 3
		run_cmd ip link del ${VRF}
		sleep 1
		log_test_addr ${a} 0 0 "${desc}, VRF server and client, local"

		setup ${with_vrf}
	done

	a=${NSA_IP}
	log_start
	run_cmd nettest ${varg} -s &
	sleep 1
	run_cmd nettest ${varg} -d ${NSA_DEV} -r ${a} &
	sleep 3
	run_cmd ip link del ${VRF}
	sleep 1
	log_test_addr ${a} 0 0 "${desc}, global server, enslaved device client, local"

	setup ${with_vrf}

	log_start
	run_cmd nettest ${varg} -I ${VRF} -s &
	sleep 1
	run_cmd nettest ${varg} -d ${NSA_DEV} -r ${a} &
	sleep 3
	run_cmd ip link del ${VRF}
	sleep 1
	log_test_addr ${a} 0 0 "${desc}, VRF server, enslaved device client, local"

	setup ${with_vrf}

	log_start
	run_cmd nettest ${varg} -I ${NSA_DEV} -s &
	sleep 1
	run_cmd nettest ${varg} -d ${NSA_DEV} -r ${a} &
	sleep 3
	run_cmd ip link del ${VRF}
	sleep 1
	log_test_addr ${a} 0 0 "${desc}, enslaved device server and client, local"
}

ipv4_ping_rt()
{
	local with_vrf="yes"
	local a

	for a in ${NSA_IP} ${VRF_IP}
	do
		log_start
		run_cmd_nsb ping -f ${a} &
		sleep 3
		run_cmd ip link del ${VRF}
		sleep 1
		log_test_addr ${a} 0 0 "Device delete with active traffic - ping in"

		setup ${with_vrf}
	done

	a=${NSB_IP}
	log_start
	run_cmd ping -f -I ${VRF} ${a} &
	sleep 3
	run_cmd ip link del ${VRF}
	sleep 1
	log_test_addr ${a} 0 0 "Device delete with active traffic - ping out"
}

ipv4_runtime()
{
	log_section "Run time tests - ipv4"

	setup "yes"
	ipv4_ping_rt

	setup "yes"
	ipv4_rt "TCP active socket"  "-n -1"

	setup "yes"
	ipv4_rt "TCP passive socket" "-i"
}

################################################################################
# IPv6

ipv6_ping_novrf()
{
	local a

	# should not have an impact, but make a known state
	set_sysctl net.ipv4.raw_l3mdev_accept=0 2>/dev/null

	#
	# out
	#
	for a in ${NSB_IP6} ${NSB_LO_IP6} ${NSB_LINKIP6}%${NSA_DEV} ${MCAST}%${NSA_DEV}
	do
		log_start
		run_cmd ${ping6} -c1 -w1 ${a}
		log_test_addr ${a} $? 0 "ping out"
	done

	for a in ${NSB_IP6} ${NSB_LO_IP6}
	do
		log_start
		run_cmd ${ping6} -c1 -w1 -I ${NSA_DEV} ${a}
		log_test_addr ${a} $? 0 "ping out, device bind"

		log_start
		run_cmd ${ping6} -c1 -w1 -I ${NSA_LO_IP6} ${a}
		log_test_addr ${a} $? 0 "ping out, loopback address bind"
	done

	#
	# in
	#
	for a in ${NSA_IP6} ${NSA_LO_IP6} ${NSA_LINKIP6}%${NSB_DEV} ${MCAST}%${NSB_DEV}
	do
		log_start
		run_cmd_nsb ${ping6} -c1 -w1 ${a}
		log_test_addr ${a} $? 0 "ping in"
	done

	#
	# local traffic, local address
	#
	for a in ${NSA_IP6} ${NSA_LO_IP6} ::1 ${NSA_LINKIP6}%${NSA_DEV} ${MCAST}%${NSA_DEV}
	do
		log_start
		run_cmd ${ping6} -c1 -w1 ${a}
		log_test_addr ${a} $? 0 "ping local, no bind"
	done

	for a in ${NSA_IP6} ${NSA_LINKIP6}%${NSA_DEV} ${MCAST}%${NSA_DEV}
	do
		log_start
		run_cmd ${ping6} -c1 -w1 -I ${NSA_DEV} ${a}
		log_test_addr ${a} $? 0 "ping local, device bind"
	done

	for a in ${NSA_LO_IP6} ::1
	do
		log_start
		show_hint "Fails since address on loopback is out of device scope"
		run_cmd ${ping6} -c1 -w1 -I ${NSA_DEV} ${a}
		log_test_addr ${a} $? 2 "ping local, device bind"
	done

	#
	# ip rule blocks address
	#
	log_start
	setup_cmd ip -6 rule add pref 32765 from all lookup local
	setup_cmd ip -6 rule del pref 0 from all lookup local
	setup_cmd ip -6 rule add pref 50 to ${NSB_LO_IP6} prohibit
	setup_cmd ip -6 rule add pref 51 from ${NSB_IP6} prohibit

	a=${NSB_LO_IP6}
	run_cmd ${ping6} -c1 -w1 ${a}
	log_test_addr ${a} $? 2 "ping out, blocked by rule"

	log_start
	run_cmd ${ping6} -c1 -w1 -I ${NSA_DEV} ${a}
	log_test_addr ${a} $? 2 "ping out, device bind, blocked by rule"

	a=${NSA_LO_IP6}
	log_start
	show_hint "Response lost due to ip rule"
	run_cmd_nsb ${ping6} -c1 -w1 ${a}
	log_test_addr ${a} $? 1 "ping in, blocked by rule"

	setup_cmd ip -6 rule add pref 0 from all lookup local
	setup_cmd ip -6 rule del pref 32765 from all lookup local
	setup_cmd ip -6 rule del pref 50 to ${NSB_LO_IP6} prohibit
	setup_cmd ip -6 rule del pref 51 from ${NSB_IP6} prohibit

	#
	# route blocks reachability to remote address
	#
	log_start
	setup_cmd ip -6 route del ${NSB_LO_IP6}
	setup_cmd ip -6 route add unreachable ${NSB_LO_IP6} metric 10
	setup_cmd ip -6 route add unreachable ${NSB_IP6} metric 10

	a=${NSB_LO_IP6}
	run_cmd ${ping6} -c1 -w1 ${a}
	log_test_addr ${a} $? 2 "ping out, blocked by route"

	log_start
	run_cmd ${ping6} -c1 -w1 -I ${NSA_DEV} ${a}
	log_test_addr ${a} $? 2 "ping out, device bind, blocked by route"

	a=${NSA_LO_IP6}
	log_start
	show_hint "Response lost due to ip route"
	run_cmd_nsb ${ping6} -c1 -w1 ${a}
	log_test_addr ${a} $? 1 "ping in, blocked by route"


	#
	# remove 'remote' routes; fallback to default
	#
	log_start
	setup_cmd ip -6 ro del unreachable ${NSB_LO_IP6}
	setup_cmd ip -6 ro del unreachable ${NSB_IP6}

	a=${NSB_LO_IP6}
	run_cmd ${ping6} -c1 -w1 ${a}
	log_test_addr ${a} $? 2 "ping out, unreachable route"

	log_start
	run_cmd ${ping6} -c1 -w1 -I ${NSA_DEV} ${a}
	log_test_addr ${a} $? 2 "ping out, device bind, unreachable route"
}

ipv6_ping_vrf()
{
	local a

	# should default on; does not exist on older kernels
	set_sysctl net.ipv4.raw_l3mdev_accept=1 2>/dev/null

	#
	# out
	#
	for a in ${NSB_IP6} ${NSB_LO_IP6}
	do
		log_start
		run_cmd ${ping6} -c1 -w1 -I ${VRF} ${a}
		log_test_addr ${a} $? 0 "ping out, VRF bind"
	done

	for a in ${NSB_LINKIP6}%${VRF} ${MCAST}%${VRF}
	do
		log_start
		show_hint "Fails since VRF device does not support linklocal or multicast"
		run_cmd ${ping6} -c1 -w1 ${a}
		log_test_addr ${a} $? 1 "ping out, VRF bind"
	done

	for a in ${NSB_IP6} ${NSB_LO_IP6} ${NSB_LINKIP6}%${NSA_DEV} ${MCAST}%${NSA_DEV}
	do
		log_start
		run_cmd ${ping6} -c1 -w1 -I ${NSA_DEV} ${a}
		log_test_addr ${a} $? 0 "ping out, device bind"
	done

	for a in ${NSB_IP6} ${NSB_LO_IP6} ${NSB_LINKIP6}%${NSA_DEV}
	do
		log_start
		run_cmd ip vrf exec ${VRF} ${ping6} -c1 -w1 -I ${VRF_IP6} ${a}
		log_test_addr ${a} $? 0 "ping out, vrf device+address bind"
	done

	#
	# in
	#
	for a in ${NSA_IP6} ${VRF_IP6} ${NSA_LINKIP6}%${NSB_DEV} ${MCAST}%${NSB_DEV}
	do
		log_start
		run_cmd_nsb ${ping6} -c1 -w1 ${a}
		log_test_addr ${a} $? 0 "ping in"
	done

	a=${NSA_LO_IP6}
	log_start
	show_hint "Fails since loopback address is out of VRF scope"
	run_cmd_nsb ${ping6} -c1 -w1 ${a}
	log_test_addr ${a} $? 1 "ping in"

	#
	# local traffic, local address
	#
	for a in ${NSA_IP6} ${VRF_IP6} ::1
	do
		log_start
		show_hint "Source address should be ${a}"
		run_cmd ${ping6} -c1 -w1 -I ${VRF} ${a}
		log_test_addr ${a} $? 0 "ping local, VRF bind"
	done

	for a in ${NSA_IP6} ${NSA_LINKIP6}%${NSA_DEV} ${MCAST}%${NSA_DEV}
	do
		log_start
		run_cmd ${ping6} -c1 -w1 -I ${NSA_DEV} ${a}
		log_test_addr ${a} $? 0 "ping local, device bind"
	done

	# LLA to GUA - remove ipv6 global addresses from ns-B
	setup_cmd_nsb ip -6 addr del ${NSB_IP6}/64 dev ${NSB_DEV}
	setup_cmd_nsb ip -6 addr del ${NSB_LO_IP6}/128 dev lo
	setup_cmd_nsb ip -6 ro add ${NSA_IP6}/128 via ${NSA_LINKIP6} dev ${NSB_DEV}

	for a in ${NSA_IP6} ${VRF_IP6}
	do
		log_start
		run_cmd_nsb ${ping6} -c1 -w1 ${NSA_IP6}
		log_test_addr ${a} $? 0 "ping in, LLA to GUA"
	done

	setup_cmd_nsb ip -6 ro del ${NSA_IP6}/128 via ${NSA_LINKIP6} dev ${NSB_DEV}
	setup_cmd_nsb ip -6 addr add ${NSB_IP6}/64 dev ${NSB_DEV}
	setup_cmd_nsb ip -6 addr add ${NSB_LO_IP6}/128 dev lo

	#
	# ip rule blocks address
	#
	log_start
	setup_cmd ip -6 rule add pref 50 to ${NSB_LO_IP6} prohibit
	setup_cmd ip -6 rule add pref 51 from ${NSB_IP6} prohibit

	a=${NSB_LO_IP6}
	run_cmd ${ping6} -c1 -w1 ${a}
	log_test_addr ${a} $? 2 "ping out, blocked by rule"

	log_start
	run_cmd ${ping6} -c1 -w1 -I ${NSA_DEV} ${a}
	log_test_addr ${a} $? 2 "ping out, device bind, blocked by rule"

	a=${NSA_LO_IP6}
	log_start
	show_hint "Response lost due to ip rule"
	run_cmd_nsb ${ping6} -c1 -w1 ${a}
	log_test_addr ${a} $? 1 "ping in, blocked by rule"

	log_start
	setup_cmd ip -6 rule del pref 50 to ${NSB_LO_IP6} prohibit
	setup_cmd ip -6 rule del pref 51 from ${NSB_IP6} prohibit

	#
	# remove 'remote' routes; fallback to default
	#
	log_start
	setup_cmd ip -6 ro del ${NSB_LO_IP6} vrf ${VRF}

	a=${NSB_LO_IP6}
	run_cmd ${ping6} -c1 -w1 ${a}
	log_test_addr ${a} $? 2 "ping out, unreachable route"

	log_start
	run_cmd ${ping6} -c1 -w1 -I ${NSA_DEV} ${a}
	log_test_addr ${a} $? 2 "ping out, device bind, unreachable route"

	ip -netns ${NSB} -6 ro del ${NSA_LO_IP6}
	a=${NSA_LO_IP6}
	log_start
	run_cmd_nsb ${ping6} -c1 -w1 ${a}
	log_test_addr ${a} $? 2 "ping in, unreachable route"
}

ipv6_ping()
{
	log_section "IPv6 ping"

	log_subsection "No VRF"
	setup
	ipv6_ping_novrf

	log_subsection "With VRF"
	setup "yes"
	ipv6_ping_vrf
}

################################################################################
# IPv6 TCP

#
# MD5 tests without VRF
#
ipv6_tcp_md5_novrf()
{
	#
	# single address
	#

	# basic use case
	log_start
	run_cmd nettest -6 -s -M ${MD5_PW} -m ${NSB_IP6} &
	sleep 1
	run_cmd_nsb nettest -6 -r ${NSA_IP6} -X ${MD5_PW}
	log_test $? 0 "MD5: Single address config"

	# client sends MD5, server not configured
	log_start
	show_hint "Should timeout due to MD5 mismatch"
	run_cmd nettest -6 -s &
	sleep 1
	run_cmd_nsb nettest -6 -r ${NSA_IP6} -X ${MD5_PW}
	log_test $? 2 "MD5: Server no config, client uses password"

	# wrong password
	log_start
	show_hint "Should timeout since client uses wrong password"
	run_cmd nettest -6 -s -M ${MD5_PW} -m ${NSB_IP6} &
	sleep 1
	run_cmd_nsb nettest -6 -r ${NSA_IP6} -X ${MD5_WRONG_PW}
	log_test $? 2 "MD5: Client uses wrong password"

	# client from different address
	log_start
	show_hint "Should timeout due to MD5 mismatch"
	run_cmd nettest -6 -s -M ${MD5_PW} -m ${NSB_LO_IP6} &
	sleep 1
	run_cmd_nsb nettest -6 -r ${NSA_IP6} -X ${MD5_PW}
	log_test $? 2 "MD5: Client address does not match address configured with password"

	#
	# MD5 extension - prefix length
	#

	# client in prefix
	log_start
	run_cmd nettest -6 -s -M ${MD5_PW} -m ${NS_NET6} &
	sleep 1
	run_cmd_nsb nettest -6 -r ${NSA_IP6} -X ${MD5_PW}
	log_test $? 0 "MD5: Prefix config"

	# client in prefix, wrong password
	log_start
	show_hint "Should timeout since client uses wrong password"
	run_cmd nettest -6 -s -M ${MD5_PW} -m ${NS_NET6} &
	sleep 1
	run_cmd_nsb nettest -6 -r ${NSA_IP6} -X ${MD5_WRONG_PW}
	log_test $? 2 "MD5: Prefix config, client uses wrong password"

	# client outside of prefix
	log_start
	show_hint "Should timeout due to MD5 mismatch"
	run_cmd nettest -6 -s -M ${MD5_PW} -m ${NS_NET6} &
	sleep 1
	run_cmd_nsb nettest -6 -c ${NSB_LO_IP6} -r ${NSA_IP6} -X ${MD5_PW}
	log_test $? 2 "MD5: Prefix config, client address not in configured prefix"
}

#
# MD5 tests with VRF
#
ipv6_tcp_md5()
{
	#
	# single address
	#

	# basic use case
	log_start
	run_cmd nettest -6 -s -I ${VRF} -M ${MD5_PW} -m ${NSB_IP6} &
	sleep 1
	run_cmd_nsb nettest -6 -r ${NSA_IP6} -X ${MD5_PW}
	log_test $? 0 "MD5: VRF: Single address config"

	# client sends MD5, server not configured
	log_start
	show_hint "Should timeout since server does not have MD5 auth"
	run_cmd nettest -6 -s -I ${VRF} &
	sleep 1
	run_cmd_nsb nettest -6 -r ${NSA_IP6} -X ${MD5_PW}
	log_test $? 2 "MD5: VRF: Server no config, client uses password"

	# wrong password
	log_start
	show_hint "Should timeout since client uses wrong password"
	run_cmd nettest -6 -s -I ${VRF} -M ${MD5_PW} -m ${NSB_IP6} &
	sleep 1
	run_cmd_nsb nettest -6 -r ${NSA_IP6} -X ${MD5_WRONG_PW}
	log_test $? 2 "MD5: VRF: Client uses wrong password"

	# client from different address
	log_start
	show_hint "Should timeout since server config differs from client"
	run_cmd nettest -6 -s -I ${VRF} -M ${MD5_PW} -m ${NSB_LO_IP6} &
	sleep 1
	run_cmd_nsb nettest -6 -r ${NSA_IP6} -X ${MD5_PW}
	log_test $? 2 "MD5: VRF: Client address does not match address configured with password"

	#
	# MD5 extension - prefix length
	#

	# client in prefix
	log_start
	run_cmd nettest -6 -s -I ${VRF} -M ${MD5_PW} -m ${NS_NET6} &
	sleep 1
	run_cmd_nsb nettest -6 -r ${NSA_IP6} -X ${MD5_PW}
	log_test $? 0 "MD5: VRF: Prefix config"

	# client in prefix, wrong password
	log_start
	show_hint "Should timeout since client uses wrong password"
	run_cmd nettest -6 -s -I ${VRF} -M ${MD5_PW} -m ${NS_NET6} &
	sleep 1
	run_cmd_nsb nettest -6 -r ${NSA_IP6} -X ${MD5_WRONG_PW}
	log_test $? 2 "MD5: VRF: Prefix config, client uses wrong password"

	# client outside of prefix
	log_start
	show_hint "Should timeout since client address is outside of prefix"
	run_cmd nettest -6 -s -I ${VRF} -M ${MD5_PW} -m ${NS_NET6} &
	sleep 1
	run_cmd_nsb nettest -6 -c ${NSB_LO_IP6} -r ${NSA_IP6} -X ${MD5_PW}
	log_test $? 2 "MD5: VRF: Prefix config, client address not in configured prefix"

	#
	# duplicate config between default VRF and a VRF
	#

	log_start
	run_cmd nettest -6 -s -I ${VRF} -M ${MD5_PW} -m ${NSB_IP6} &
	run_cmd nettest -6 -s -M ${MD5_WRONG_PW} -m ${NSB_IP6} &
	sleep 1
	run_cmd_nsb nettest -6 -r ${NSA_IP6} -X ${MD5_PW}
	log_test $? 0 "MD5: VRF: Single address config in default VRF and VRF, conn in VRF"

	log_start
	run_cmd nettest -6 -s -I ${VRF} -M ${MD5_PW} -m ${NSB_IP6} &
	run_cmd nettest -6 -s -M ${MD5_WRONG_PW} -m ${NSB_IP6} &
	sleep 1
	run_cmd_nsc nettest -6 -r ${NSA_IP6} -X ${MD5_WRONG_PW}
	log_test $? 0 "MD5: VRF: Single address config in default VRF and VRF, conn in default VRF"

	log_start
	show_hint "Should timeout since client in default VRF uses VRF password"
	run_cmd nettest -6 -s -I ${VRF} -M ${MD5_PW} -m ${NSB_IP6} &
	run_cmd nettest -6 -s -M ${MD5_WRONG_PW} -m ${NSB_IP6} &
	sleep 1
	run_cmd_nsc nettest -6 -r ${NSA_IP6} -X ${MD5_PW}
	log_test $? 2 "MD5: VRF: Single address config in default VRF and VRF, conn in default VRF with VRF pw"

	log_start
	show_hint "Should timeout since client in VRF uses default VRF password"
	run_cmd nettest -6 -s -I ${VRF} -M ${MD5_PW} -m ${NSB_IP6} &
	run_cmd nettest -6 -s -M ${MD5_WRONG_PW} -m ${NSB_IP6} &
	sleep 1
	run_cmd_nsb nettest -6 -r ${NSA_IP6} -X ${MD5_WRONG_PW}
	log_test $? 2 "MD5: VRF: Single address config in default VRF and VRF, conn in VRF with default VRF pw"

	log_start
	run_cmd nettest -6 -s -I ${VRF} -M ${MD5_PW} -m ${NS_NET6} &
	run_cmd nettest -6 -s -M ${MD5_WRONG_PW} -m ${NS_NET6} &
	sleep 1
	run_cmd_nsb nettest -6 -r ${NSA_IP6} -X ${MD5_PW}
	log_test $? 0 "MD5: VRF: Prefix config in default VRF and VRF, conn in VRF"

	log_start
	run_cmd nettest -6 -s -I ${VRF} -M ${MD5_PW} -m ${NS_NET6} &
	run_cmd nettest -6 -s -M ${MD5_WRONG_PW} -m ${NS_NET6} &
	sleep 1
	run_cmd_nsc nettest -6 -r ${NSA_IP6} -X ${MD5_WRONG_PW}
	log_test $? 0 "MD5: VRF: Prefix config in default VRF and VRF, conn in default VRF"

	log_start
	show_hint "Should timeout since client in default VRF uses VRF password"
	run_cmd nettest -6 -s -I ${VRF} -M ${MD5_PW} -m ${NS_NET6} &
	run_cmd nettest -6 -s -M ${MD5_WRONG_PW} -m ${NS_NET6} &
	sleep 1
	run_cmd_nsc nettest -6 -r ${NSA_IP6} -X ${MD5_PW}
	log_test $? 2 "MD5: VRF: Prefix config in default VRF and VRF, conn in default VRF with VRF pw"

	log_start
	show_hint "Should timeout since client in VRF uses default VRF password"
	run_cmd nettest -6 -s -I ${VRF} -M ${MD5_PW} -m ${NS_NET6} &
	run_cmd nettest -6 -s -M ${MD5_WRONG_PW} -m ${NS_NET6} &
	sleep 1
	run_cmd_nsb nettest -6 -r ${NSA_IP6} -X ${MD5_WRONG_PW}
	log_test $? 2 "MD5: VRF: Prefix config in default VRF and VRF, conn in VRF with default VRF pw"

	#
	# negative tests
	#
	log_start
	run_cmd nettest -6 -s -I ${NSA_DEV} -M ${MD5_PW} -m ${NSB_IP6}
	log_test $? 1 "MD5: VRF: Device must be a VRF - single address"

	log_start
	run_cmd nettest -6 -s -I ${NSA_DEV} -M ${MD5_PW} -m ${NS_NET6}
	log_test $? 1 "MD5: VRF: Device must be a VRF - prefix"

}

ipv6_tcp_novrf()
{
	local a

	#
	# server tests
	#
	for a in ${NSA_IP6} ${NSA_LO_IP6} ${NSA_LINKIP6}%${NSB_DEV}
	do
		log_start
		run_cmd nettest -6 -s &
		sleep 1
		run_cmd_nsb nettest -6 -r ${a}
		log_test_addr ${a} $? 0 "Global server"
	done

	# verify TCP reset received
	for a in ${NSA_IP6} ${NSA_LO_IP6} ${NSA_LINKIP6}%${NSB_DEV}
	do
		log_start
		show_hint "Should fail 'Connection refused'"
		run_cmd_nsb nettest -6 -r ${a}
		log_test_addr ${a} $? 1 "No server"
	done

	#
	# client
	#
	for a in ${NSB_IP6} ${NSB_LO_IP6} ${NSB_LINKIP6}%${NSA_DEV}
	do
		log_start
		run_cmd_nsb nettest -6 -s &
		sleep 1
		run_cmd nettest -6 -r ${a}
		log_test_addr ${a} $? 0 "Client"
	done

	for a in ${NSB_IP6} ${NSB_LO_IP6} ${NSB_LINKIP6}%${NSA_DEV}
	do
		log_start
		run_cmd_nsb nettest -6 -s &
		sleep 1
		run_cmd nettest -6 -r ${a} -d ${NSA_DEV}
		log_test_addr ${a} $? 0 "Client, device bind"
	done

	for a in ${NSB_IP6} ${NSB_LO_IP6} ${NSB_LINKIP6}%${NSA_DEV}
	do
		log_start
		show_hint "Should fail 'Connection refused'"
		run_cmd nettest -6 -r ${a} -d ${NSA_DEV}
		log_test_addr ${a} $? 1 "No server, device client"
	done

	#
	# local address tests
	#
	for a in ${NSA_IP6} ${NSA_LO_IP6} ::1
	do
		log_start
		run_cmd nettest -6 -s &
		sleep 1
		run_cmd nettest -6 -r ${a}
		log_test_addr ${a} $? 0 "Global server, local connection"
	done

	a=${NSA_IP6}
	log_start
	run_cmd nettest -6 -s -I ${NSA_DEV} -3 ${NSA_DEV} &
	sleep 1
	run_cmd nettest -6 -r ${a} -0 ${a}
	log_test_addr ${a} $? 0 "Device server, unbound client, local connection"

	for a in ${NSA_LO_IP6} ::1
	do
		log_start
		show_hint "Should fail 'Connection refused' since addresses on loopback are out of device scope"
		run_cmd nettest -6 -s -I ${NSA_DEV} &
		sleep 1
		run_cmd nettest -6 -r ${a}
		log_test_addr ${a} $? 1 "Device server, unbound client, local connection"
	done

	a=${NSA_IP6}
	log_start
	run_cmd nettest -6 -s &
	sleep 1
	run_cmd nettest -6 -r ${a} -d ${NSA_DEV} -0 ${a}
	log_test_addr ${a} $? 0 "Global server, device client, local connection"

	for a in ${NSA_LO_IP6} ::1
	do
		log_start
		show_hint "Should fail 'Connection refused' since addresses on loopback are out of device scope"
		run_cmd nettest -6 -s &
		sleep 1
		run_cmd nettest -6 -r ${a} -d ${NSA_DEV}
		log_test_addr ${a} $? 1 "Global server, device client, local connection"
	done

	for a in ${NSA_IP6} ${NSA_LINKIP6}
	do
		log_start
		run_cmd nettest -6 -s -I ${NSA_DEV} -3 ${NSA_DEV} &
		sleep 1
		run_cmd nettest -6  -d ${NSA_DEV} -r ${a}
		log_test_addr ${a} $? 0 "Device server, device client, local conn"
	done

	for a in ${NSA_IP6} ${NSA_LINKIP6}
	do
		log_start
		show_hint "Should fail 'Connection refused'"
		run_cmd nettest -6 -d ${NSA_DEV} -r ${a}
		log_test_addr ${a} $? 1 "No server, device client, local conn"
	done

	ipv6_tcp_md5_novrf
}

ipv6_tcp_vrf()
{
	local a

	# disable global server
	log_subsection "Global server disabled"

	set_sysctl net.ipv4.tcp_l3mdev_accept=0

	#
	# server tests
	#
	for a in ${NSA_IP6} ${VRF_IP6} ${NSA_LINKIP6}%${NSB_DEV}
	do
		log_start
		show_hint "Should fail 'Connection refused' since global server with VRF is disabled"
		run_cmd nettest -6 -s &
		sleep 1
		run_cmd_nsb nettest -6 -r ${a}
		log_test_addr ${a} $? 1 "Global server"
	done

	for a in ${NSA_IP6} ${VRF_IP6}
	do
		log_start
		run_cmd nettest -6 -s -I ${VRF} -3 ${VRF} &
		sleep 1
		run_cmd_nsb nettest -6 -r ${a}
		log_test_addr ${a} $? 0 "VRF server"
	done

	# link local is always bound to ingress device
	a=${NSA_LINKIP6}%${NSB_DEV}
	log_start
	run_cmd nettest -6 -s -I ${VRF} -3 ${NSA_DEV} &
	sleep 1
	run_cmd_nsb nettest -6 -r ${a}
	log_test_addr ${a} $? 0 "VRF server"

	for a in ${NSA_IP6} ${VRF_IP6} ${NSA_LINKIP6}%${NSB_DEV}
	do
		log_start
		run_cmd nettest -6 -s -I ${NSA_DEV} -3 ${NSA_DEV} &
		sleep 1
		run_cmd_nsb nettest -6 -r ${a}
		log_test_addr ${a} $? 0 "Device server"
	done

	# verify TCP reset received
	for a in ${NSA_IP6} ${VRF_IP6} ${NSA_LINKIP6}%${NSB_DEV}
	do
		log_start
		show_hint "Should fail 'Connection refused'"
		run_cmd_nsb nettest -6 -r ${a}
		log_test_addr ${a} $? 1 "No server"
	done

	# local address tests
	a=${NSA_IP6}
	log_start
	show_hint "Should fail 'Connection refused' since global server with VRF is disabled"
	run_cmd nettest -6 -s &
	sleep 1
	run_cmd nettest -6 -r ${a} -d ${NSA_DEV}
	log_test_addr ${a} $? 1 "Global server, local connection"

	# run MD5 tests
	setup_vrf_dup
	ipv6_tcp_md5
	cleanup_vrf_dup

	#
	# enable VRF global server
	#
	log_subsection "VRF Global server enabled"
	set_sysctl net.ipv4.tcp_l3mdev_accept=1

	for a in ${NSA_IP6} ${VRF_IP6}
	do
		log_start
		run_cmd nettest -6 -s -3 ${VRF} &
		sleep 1
		run_cmd_nsb nettest -6 -r ${a}
		log_test_addr ${a} $? 0 "Global server"
	done

	for a in ${NSA_IP6} ${VRF_IP6}
	do
		log_start
		run_cmd nettest -6 -s -I ${VRF} -3 ${VRF} &
		sleep 1
		run_cmd_nsb nettest -6 -r ${a}
		log_test_addr ${a} $? 0 "VRF server"
	done

	# For LLA, child socket is bound to device
	a=${NSA_LINKIP6}%${NSB_DEV}
	log_start
	run_cmd nettest -6 -s -3 ${NSA_DEV} &
	sleep 1
	run_cmd_nsb nettest -6 -r ${a}
	log_test_addr ${a} $? 0 "Global server"

	log_start
	run_cmd nettest -6 -s -I ${VRF} -3 ${NSA_DEV} &
	sleep 1
	run_cmd_nsb nettest -6 -r ${a}
	log_test_addr ${a} $? 0 "VRF server"

	for a in ${NSA_IP6} ${NSA_LINKIP6}%${NSB_DEV}
	do
		log_start
		run_cmd nettest -6 -s -I ${NSA_DEV} -3 ${NSA_DEV} &
		sleep 1
		run_cmd_nsb nettest -6 -r ${a}
		log_test_addr ${a} $? 0 "Device server"
	done

	# verify TCP reset received
	for a in ${NSA_IP6} ${VRF_IP6} ${NSA_LINKIP6}%${NSB_DEV}
	do
		log_start
		show_hint "Should fail 'Connection refused'"
		run_cmd_nsb nettest -6 -r ${a}
		log_test_addr ${a} $? 1 "No server"
	done

	# local address tests
	for a in ${NSA_IP6} ${VRF_IP6}
	do
		log_start
		show_hint "Fails 'Connection refused' since client is not in VRF"
		run_cmd nettest -6 -s -I ${VRF} &
		sleep 1
		run_cmd nettest -6 -r ${a}
		log_test_addr ${a} $? 1 "Global server, local connection"
	done


	#
	# client
	#
	for a in ${NSB_IP6} ${NSB_LO_IP6}
	do
		log_start
		run_cmd_nsb nettest -6 -s &
		sleep 1
		run_cmd nettest -6 -r ${a} -d ${VRF}
		log_test_addr ${a} $? 0 "Client, VRF bind"
	done

	a=${NSB_LINKIP6}
	log_start
	show_hint "Fails since VRF device does not allow linklocal addresses"
	run_cmd_nsb nettest -6 -s &
	sleep 1
	run_cmd nettest -6 -r ${a} -d ${VRF}
	log_test_addr ${a} $? 1 "Client, VRF bind"

	for a in ${NSB_IP6} ${NSB_LO_IP6} ${NSB_LINKIP6}
	do
		log_start
		run_cmd_nsb nettest -6 -s &
		sleep 1
		run_cmd nettest -6 -r ${a} -d ${NSA_DEV}
		log_test_addr ${a} $? 0 "Client, device bind"
	done

	for a in ${NSB_IP6} ${NSB_LO_IP6}
	do
		log_start
		show_hint "Should fail 'Connection refused'"
		run_cmd nettest -6 -r ${a} -d ${VRF}
		log_test_addr ${a} $? 1 "No server, VRF client"
	done

	for a in ${NSB_IP6} ${NSB_LO_IP6} ${NSB_LINKIP6}
	do
		log_start
		show_hint "Should fail 'Connection refused'"
		run_cmd nettest -6 -r ${a} -d ${NSA_DEV}
		log_test_addr ${a} $? 1 "No server, device client"
	done

	for a in ${NSA_IP6} ${VRF_IP6} ::1
	do
		log_start
		run_cmd nettest -6 -s -I ${VRF} -3 ${VRF} &
		sleep 1
		run_cmd nettest -6 -r ${a} -d ${VRF} -0 ${a}
		log_test_addr ${a} $? 0 "VRF server, VRF client, local connection"
	done

	a=${NSA_IP6}
	log_start
	run_cmd nettest -6 -s -I ${VRF} -3 ${VRF} &
	sleep 1
	run_cmd nettest -6 -r ${a} -d ${NSA_DEV} -0 ${a}
	log_test_addr ${a} $? 0 "VRF server, device client, local connection"

	a=${NSA_IP6}
	log_start
	show_hint "Should fail since unbound client is out of VRF scope"
	run_cmd nettest -6 -s -I ${VRF} &
	sleep 1
	run_cmd nettest -6 -r ${a}
	log_test_addr ${a} $? 1 "VRF server, unbound client, local connection"

	log_start
	run_cmd nettest -6 -s -I ${NSA_DEV} -3 ${NSA_DEV} &
	sleep 1
	run_cmd nettest -6 -r ${a} -d ${VRF} -0 ${a}
	log_test_addr ${a} $? 0 "Device server, VRF client, local connection"

	for a in ${NSA_IP6} ${NSA_LINKIP6}
	do
		log_start
		run_cmd nettest -6 -s -I ${NSA_DEV} -3 ${NSA_DEV} &
		sleep 1
		run_cmd nettest -6 -r ${a} -d ${NSA_DEV} -0 ${a}
		log_test_addr ${a} $? 0 "Device server, device client, local connection"
	done
}

ipv6_tcp()
{
	log_section "IPv6/TCP"
	log_subsection "No VRF"
	setup

	# tcp_l3mdev_accept should have no affect without VRF;
	# run tests with it enabled and disabled to verify
	log_subsection "tcp_l3mdev_accept disabled"
	set_sysctl net.ipv4.tcp_l3mdev_accept=0
	ipv6_tcp_novrf
	log_subsection "tcp_l3mdev_accept enabled"
	set_sysctl net.ipv4.tcp_l3mdev_accept=1
	ipv6_tcp_novrf

	log_subsection "With VRF"
	setup "yes"
	ipv6_tcp_vrf
}

################################################################################
# IPv6 UDP

ipv6_udp_novrf()
{
	local a

	#
	# server tests
	#
	for a in ${NSA_IP6} ${NSA_LINKIP6}%${NSB_DEV}
	do
		log_start
		run_cmd nettest -6 -D -s -3 ${NSA_DEV} &
		sleep 1
		run_cmd_nsb nettest -6 -D -r ${a}
		log_test_addr ${a} $? 0 "Global server"

		log_start
		run_cmd nettest -6 -D -I ${NSA_DEV} -s -3 ${NSA_DEV} &
		sleep 1
		run_cmd_nsb nettest -6 -D -r ${a}
		log_test_addr ${a} $? 0 "Device server"
	done

	a=${NSA_LO_IP6}
	log_start
	run_cmd nettest -6 -D -s -3 ${NSA_DEV} &
	sleep 1
	run_cmd_nsb nettest -6 -D -r ${a}
	log_test_addr ${a} $? 0 "Global server"

	# should fail since loopback address is out of scope for a device
	# bound server, but it does not - hence this is more documenting
	# behavior.
	#log_start
	#show_hint "Should fail since loopback address is out of scope"
	#run_cmd nettest -6 -D -I ${NSA_DEV} -s -3 ${NSA_DEV} &
	#sleep 1
	#run_cmd_nsb nettest -6 -D -r ${a}
	#log_test_addr ${a} $? 1 "Device server"

	# negative test - should fail
	for a in ${NSA_IP6} ${NSA_LO_IP6} ${NSA_LINKIP6}%${NSB_DEV}
	do
		log_start
		show_hint "Should fail 'Connection refused' since there is no server"
		run_cmd_nsb nettest -6 -D -r ${a}
		log_test_addr ${a} $? 1 "No server"
	done

	#
	# client
	#
	for a in ${NSB_IP6} ${NSB_LO_IP6} ${NSB_LINKIP6}%${NSA_DEV}
	do
		log_start
		run_cmd_nsb nettest -6 -D -s &
		sleep 1
		run_cmd nettest -6 -D -r ${a} -0 ${NSA_IP6}
		log_test_addr ${a} $? 0 "Client"

		log_start
		run_cmd_nsb nettest -6 -D -s &
		sleep 1
		run_cmd nettest -6 -D -r ${a} -d ${NSA_DEV} -0 ${NSA_IP6}
		log_test_addr ${a} $? 0 "Client, device bind"

		log_start
		run_cmd_nsb nettest -6 -D -s &
		sleep 1
		run_cmd nettest -6 -D -r ${a} -d ${NSA_DEV} -C -0 ${NSA_IP6}
		log_test_addr ${a} $? 0 "Client, device send via cmsg"

		log_start
		run_cmd_nsb nettest -6 -D -s &
		sleep 1
		run_cmd nettest -6 -D -r ${a} -d ${NSA_DEV} -S -0 ${NSA_IP6}
		log_test_addr ${a} $? 0 "Client, device bind via IPV6_UNICAST_IF"

		log_start
		show_hint "Should fail 'Connection refused'"
		run_cmd nettest -6 -D -r ${a}
		log_test_addr ${a} $? 1 "No server, unbound client"

		log_start
		show_hint "Should fail 'Connection refused'"
		run_cmd nettest -6 -D -r ${a} -d ${NSA_DEV}
		log_test_addr ${a} $? 1 "No server, device client"
	done

	#
	# local address tests
	#
	for a in ${NSA_IP6} ${NSA_LO_IP6} ::1
	do
		log_start
		run_cmd nettest -6 -D -s &
		sleep 1
		run_cmd nettest -6 -D -r ${a} -0 ${a} -1 ${a}
		log_test_addr ${a} $? 0 "Global server, local connection"
	done

	a=${NSA_IP6}
	log_start
	run_cmd nettest -6 -s -D -I ${NSA_DEV} -3 ${NSA_DEV} &
	sleep 1
	run_cmd nettest -6 -D -r ${a}
	log_test_addr ${a} $? 0 "Device server, unbound client, local connection"

	for a in ${NSA_LO_IP6} ::1
	do
		log_start
		show_hint "Should fail 'Connection refused' since address is out of device scope"
		run_cmd nettest -6 -s -D -I ${NSA_DEV} &
		sleep 1
		run_cmd nettest -6 -D -r ${a}
		log_test_addr ${a} $? 1 "Device server, local connection"
	done

	a=${NSA_IP6}
	log_start
	run_cmd nettest -6 -s -D &
	sleep 1
	run_cmd nettest -6 -D -d ${NSA_DEV} -r ${a}
	log_test_addr ${a} $? 0 "Global server, device client, local connection"

	log_start
	run_cmd nettest -6 -s -D &
	sleep 1
	run_cmd nettest -6 -D -d ${NSA_DEV} -C -r ${a}
	log_test_addr ${a} $? 0 "Global server, device send via cmsg, local connection"

	log_start
	run_cmd nettest -6 -s -D &
	sleep 1
	run_cmd nettest -6 -D -d ${NSA_DEV} -S -r ${a}
	log_test_addr ${a} $? 0 "Global server, device client via IPV6_UNICAST_IF, local connection"

	for a in ${NSA_LO_IP6} ::1
	do
		log_start
		show_hint "Should fail 'No route to host' since addresses on loopback are out of device scope"
		run_cmd nettest -6 -D -s &
		sleep 1
		run_cmd nettest -6 -D -r ${a} -d ${NSA_DEV}
		log_test_addr ${a} $? 1 "Global server, device client, local connection"

		log_start
		show_hint "Should fail 'No route to host' since addresses on loopback are out of device scope"
		run_cmd nettest -6 -D -s &
		sleep 1
		run_cmd nettest -6 -D -r ${a} -d ${NSA_DEV} -C
		log_test_addr ${a} $? 1 "Global server, device send via cmsg, local connection"

		log_start
		show_hint "Should fail 'No route to host' since addresses on loopback are out of device scope"
		run_cmd nettest -6 -D -s &
		sleep 1
		run_cmd nettest -6 -D -r ${a} -d ${NSA_DEV} -S
		log_test_addr ${a} $? 1 "Global server, device client via IP_UNICAST_IF, local connection"
	done

	a=${NSA_IP6}
	log_start
	run_cmd nettest -6 -D -s -I ${NSA_DEV} -3 ${NSA_DEV} &
	sleep 1
	run_cmd nettest -6 -D -d ${NSA_DEV} -r ${a} -0 ${a}
	log_test_addr ${a} $? 0 "Device server, device client, local conn"

	log_start
	show_hint "Should fail 'Connection refused'"
	run_cmd nettest -6 -D -d ${NSA_DEV} -r ${a}
	log_test_addr ${a} $? 1 "No server, device client, local conn"

	# LLA to GUA
	run_cmd_nsb ip -6 addr del ${NSB_IP6}/64 dev ${NSB_DEV}
	run_cmd_nsb ip -6 ro add ${NSA_IP6}/128 dev ${NSB_DEV}
	log_start
	run_cmd nettest -6 -s -D &
	sleep 1
	run_cmd_nsb nettest -6 -D -r ${NSA_IP6}
	log_test $? 0 "UDP in - LLA to GUA"

	run_cmd_nsb ip -6 ro del ${NSA_IP6}/128 dev ${NSB_DEV}
	run_cmd_nsb ip -6 addr add ${NSB_IP6}/64 dev ${NSB_DEV} nodad
}

ipv6_udp_vrf()
{
	local a

	# disable global server
	log_subsection "Global server disabled"
	set_sysctl net.ipv4.udp_l3mdev_accept=0

	#
	# server tests
	#
	for a in ${NSA_IP6} ${VRF_IP6}
	do
		log_start
		show_hint "Should fail 'Connection refused' since global server is disabled"
		run_cmd nettest -6 -D -s &
		sleep 1
		run_cmd_nsb nettest -6 -D -r ${a}
		log_test_addr ${a} $? 1 "Global server"
	done

	for a in ${NSA_IP6} ${VRF_IP6}
	do
		log_start
		run_cmd nettest -6 -D -I ${VRF} -s -3 ${NSA_DEV} &
		sleep 1
		run_cmd_nsb nettest -6 -D -r ${a}
		log_test_addr ${a} $? 0 "VRF server"
	done

	for a in ${NSA_IP6} ${VRF_IP6}
	do
		log_start
		run_cmd nettest -6 -D -I ${NSA_DEV} -s -3 ${NSA_DEV} &
		sleep 1
		run_cmd_nsb nettest -6 -D -r ${a}
		log_test_addr ${a} $? 0 "Enslaved device server"
	done

	# negative test - should fail
	for a in ${NSA_IP6} ${VRF_IP6}
	do
		log_start
		show_hint "Should fail 'Connection refused' since there is no server"
		run_cmd_nsb nettest -6 -D -r ${a}
		log_test_addr ${a} $? 1 "No server"
	done

	#
	# local address tests
	#
	for a in ${NSA_IP6} ${VRF_IP6}
	do
		log_start
		show_hint "Should fail 'Connection refused' since global server is disabled"
		run_cmd nettest -6 -D -s &
		sleep 1
		run_cmd nettest -6 -D -d ${VRF} -r ${a}
		log_test_addr ${a} $? 1 "Global server, VRF client, local conn"
	done

	for a in ${NSA_IP6} ${VRF_IP6}
	do
		log_start
		run_cmd nettest -6 -D -I ${VRF} -s &
		sleep 1
		run_cmd nettest -6 -D -d ${VRF} -r ${a}
		log_test_addr ${a} $? 0 "VRF server, VRF client, local conn"
	done

	a=${NSA_IP6}
	log_start
	show_hint "Should fail 'Connection refused' since global server is disabled"
	run_cmd nettest -6 -D -s &
	sleep 1
	run_cmd nettest -6 -D -d ${NSA_DEV} -r ${a}
	log_test_addr ${a} $? 1 "Global server, device client, local conn"

	log_start
	run_cmd nettest -6 -D -I ${VRF} -s -3 ${NSA_DEV} &
	sleep 1
	run_cmd nettest -6 -D -d ${NSA_DEV} -r ${a}
	log_test_addr ${a} $? 0 "VRF server, device client, local conn"

	log_start
	run_cmd nettest -6 -D -I ${NSA_DEV} -s -3 ${NSA_DEV} &
	sleep 1
	run_cmd nettest -6 -D -d ${VRF} -r ${a}
	log_test_addr ${a} $? 0 "Enslaved device server, VRF client, local conn"

	log_start
	run_cmd nettest -6 -D -I ${NSA_DEV} -s -3 ${NSA_DEV} &
	sleep 1
	run_cmd nettest -6 -D -d ${NSA_DEV} -r ${a}
	log_test_addr ${a} $? 0 "Enslaved device server, device client, local conn"

	# disable global server
	log_subsection "Global server enabled"
	set_sysctl net.ipv4.udp_l3mdev_accept=1

	#
	# server tests
	#
	for a in ${NSA_IP6} ${VRF_IP6}
	do
		log_start
		run_cmd nettest -6 -D -s -3 ${NSA_DEV} &
		sleep 1
		run_cmd_nsb nettest -6 -D -r ${a}
		log_test_addr ${a} $? 0 "Global server"
	done

	for a in ${NSA_IP6} ${VRF_IP6}
	do
		log_start
		run_cmd nettest -6 -D -I ${VRF} -s -3 ${NSA_DEV} &
		sleep 1
		run_cmd_nsb nettest -6 -D -r ${a}
		log_test_addr ${a} $? 0 "VRF server"
	done

	for a in ${NSA_IP6} ${VRF_IP6}
	do
		log_start
		run_cmd nettest -6 -D -I ${NSA_DEV} -s -3 ${NSA_DEV} &
		sleep 1
		run_cmd_nsb nettest -6 -D -r ${a}
		log_test_addr ${a} $? 0 "Enslaved device server"
	done

	# negative test - should fail
	for a in ${NSA_IP6} ${VRF_IP6}
	do
		log_start
		run_cmd_nsb nettest -6 -D -r ${a}
		log_test_addr ${a} $? 1 "No server"
	done

	#
	# client tests
	#
	log_start
	run_cmd_nsb nettest -6 -D -s &
	sleep 1
	run_cmd nettest -6 -D -d ${VRF} -r ${NSB_IP6}
	log_test $? 0 "VRF client"

	# negative test - should fail
	log_start
	run_cmd nettest -6 -D -d ${VRF} -r ${NSB_IP6}
	log_test $? 1 "No server, VRF client"

	log_start
	run_cmd_nsb nettest -6 -D -s &
	sleep 1
	run_cmd nettest -6 -D -d ${NSA_DEV} -r ${NSB_IP6}
	log_test $? 0 "Enslaved device client"

	# negative test - should fail
	log_start
	run_cmd nettest -6 -D -d ${NSA_DEV} -r ${NSB_IP6}
	log_test $? 1 "No server, enslaved device client"

	#
	# local address tests
	#
	a=${NSA_IP6}
	log_start
	run_cmd nettest -6 -D -s -3 ${NSA_DEV} &
	sleep 1
	run_cmd nettest -6 -D -d ${VRF} -r ${a}
	log_test_addr ${a} $? 0 "Global server, VRF client, local conn"

	#log_start
	run_cmd nettest -6 -D -I ${VRF} -s -3 ${NSA_DEV} &
	sleep 1
	run_cmd nettest -6 -D -d ${VRF} -r ${a}
	log_test_addr ${a} $? 0 "VRF server, VRF client, local conn"


	a=${VRF_IP6}
	log_start
	run_cmd nettest -6 -D -s -3 ${VRF} &
	sleep 1
	run_cmd nettest -6 -D -d ${VRF} -r ${a}
	log_test_addr ${a} $? 0 "Global server, VRF client, local conn"

	log_start
	run_cmd nettest -6 -D -I ${VRF} -s -3 ${VRF} &
	sleep 1
	run_cmd nettest -6 -D -d ${VRF} -r ${a}
	log_test_addr ${a} $? 0 "VRF server, VRF client, local conn"

	# negative test - should fail
	for a in ${NSA_IP6} ${VRF_IP6}
	do
		log_start
		run_cmd nettest -6 -D -d ${VRF} -r ${a}
		log_test_addr ${a} $? 1 "No server, VRF client, local conn"
	done

	# device to global IP
	a=${NSA_IP6}
	log_start
	run_cmd nettest -6 -D -s -3 ${NSA_DEV} &
	sleep 1
	run_cmd nettest -6 -D -d ${NSA_DEV} -r ${a}
	log_test_addr ${a} $? 0 "Global server, device client, local conn"

	log_start
	run_cmd nettest -6 -D -I ${VRF} -s -3 ${NSA_DEV} &
	sleep 1
	run_cmd nettest -6 -D -d ${NSA_DEV} -r ${a}
	log_test_addr ${a} $? 0 "VRF server, device client, local conn"

	log_start
	run_cmd nettest -6 -D -I ${NSA_DEV} -s -3 ${NSA_DEV} &
	sleep 1
	run_cmd nettest -6 -D -d ${VRF} -r ${a}
	log_test_addr ${a} $? 0 "Device server, VRF client, local conn"

	log_start
	run_cmd nettest -6 -D -I ${NSA_DEV} -s -3 ${NSA_DEV} &
	sleep 1
	run_cmd nettest -6 -D -d ${NSA_DEV} -r ${a}
	log_test_addr ${a} $? 0 "Device server, device client, local conn"

	log_start
	run_cmd nettest -6 -D -d ${NSA_DEV} -r ${a}
	log_test_addr ${a} $? 1 "No server, device client, local conn"


	# link local addresses
	log_start
	run_cmd nettest -6 -D -s &
	sleep 1
	run_cmd_nsb nettest -6 -D -d ${NSB_DEV} -r ${NSA_LINKIP6}
	log_test $? 0 "Global server, linklocal IP"

	log_start
	run_cmd_nsb nettest -6 -D -d ${NSB_DEV} -r ${NSA_LINKIP6}
	log_test $? 1 "No server, linklocal IP"


	log_start
	run_cmd_nsb nettest -6 -D -s &
	sleep 1
	run_cmd nettest -6 -D -d ${NSA_DEV} -r ${NSB_LINKIP6}
	log_test $? 0 "Enslaved device client, linklocal IP"

	log_start
	run_cmd nettest -6 -D -d ${NSA_DEV} -r ${NSB_LINKIP6}
	log_test $? 1 "No server, device client, peer linklocal IP"


	log_start
	run_cmd nettest -6 -D -s &
	sleep 1
	run_cmd nettest -6 -D -d ${NSA_DEV} -r ${NSA_LINKIP6}
	log_test $? 0 "Enslaved device client, local conn - linklocal IP"

	log_start
	run_cmd nettest -6 -D -d ${NSA_DEV} -r ${NSA_LINKIP6}
	log_test $? 1 "No server, device client, local conn  - linklocal IP"

	# LLA to GUA
	run_cmd_nsb ip -6 addr del ${NSB_IP6}/64 dev ${NSB_DEV}
	run_cmd_nsb ip -6 ro add ${NSA_IP6}/128 dev ${NSB_DEV}
	log_start
	run_cmd nettest -6 -s -D &
	sleep 1
	run_cmd_nsb nettest -6 -D -r ${NSA_IP6}
	log_test $? 0 "UDP in - LLA to GUA"

	run_cmd_nsb ip -6 ro del ${NSA_IP6}/128 dev ${NSB_DEV}
	run_cmd_nsb ip -6 addr add ${NSB_IP6}/64 dev ${NSB_DEV} nodad
}

ipv6_udp()
{
        # should not matter, but set to known state
        set_sysctl net.ipv4.udp_early_demux=1

        log_section "IPv6/UDP"
        log_subsection "No VRF"
        setup

        # udp_l3mdev_accept should have no affect without VRF;
        # run tests with it enabled and disabled to verify
        log_subsection "udp_l3mdev_accept disabled"
        set_sysctl net.ipv4.udp_l3mdev_accept=0
        ipv6_udp_novrf
        log_subsection "udp_l3mdev_accept enabled"
        set_sysctl net.ipv4.udp_l3mdev_accept=1
        ipv6_udp_novrf

        log_subsection "With VRF"
        setup "yes"
        ipv6_udp_vrf
}

################################################################################
# IPv6 address bind

ipv6_addr_bind_novrf()
{
	#
	# raw socket
	#
	for a in ${NSA_IP6} ${NSA_LO_IP6}
	do
		log_start
		run_cmd nettest -6 -s -R -P ipv6-icmp -l ${a} -b
		log_test_addr ${a} $? 0 "Raw socket bind to local address"

		log_start
		run_cmd nettest -6 -s -R -P ipv6-icmp -l ${a} -I ${NSA_DEV} -b
		log_test_addr ${a} $? 0 "Raw socket bind to local address after device bind"
	done

	#
	# tcp sockets
	#
	a=${NSA_IP6}
	log_start
	run_cmd nettest -6 -s -l ${a} -t1 -b
	log_test_addr ${a} $? 0 "TCP socket bind to local address"

	log_start
	run_cmd nettest -6 -s -l ${a} -I ${NSA_DEV} -t1 -b
	log_test_addr ${a} $? 0 "TCP socket bind to local address after device bind"

	# Sadly, the kernel allows binding a socket to a device and then
	# binding to an address not on the device. So this test passes
	# when it really should not
	a=${NSA_LO_IP6}
	log_start
	show_hint "Tecnically should fail since address is not on device but kernel allows"
	run_cmd nettest -6 -s -l ${a} -I ${NSA_DEV} -t1 -b
	log_test_addr ${a} $? 0 "TCP socket bind to out of scope local address"
}

ipv6_addr_bind_vrf()
{
	#
	# raw socket
	#
	for a in ${NSA_IP6} ${VRF_IP6}
	do
		log_start
		run_cmd nettest -6 -s -R -P ipv6-icmp -l ${a} -I ${VRF} -b
		log_test_addr ${a} $? 0 "Raw socket bind to local address after vrf bind"

		log_start
		run_cmd nettest -6 -s -R -P ipv6-icmp -l ${a} -I ${NSA_DEV} -b
		log_test_addr ${a} $? 0 "Raw socket bind to local address after device bind"
	done

	a=${NSA_LO_IP6}
	log_start
	show_hint "Address on loopback is out of VRF scope"
	run_cmd nettest -6 -s -R -P ipv6-icmp -l ${a} -I ${VRF} -b
	log_test_addr ${a} $? 1 "Raw socket bind to invalid local address after vrf bind"

	#
	# tcp sockets
	#
	# address on enslaved device is valid for the VRF or device in a VRF
	for a in ${NSA_IP6} ${VRF_IP6}
	do
		log_start
		run_cmd nettest -6 -s -l ${a} -I ${VRF} -t1 -b
		log_test_addr ${a} $? 0 "TCP socket bind to local address with VRF bind"
	done

	a=${NSA_IP6}
	log_start
	run_cmd nettest -6 -s -l ${a} -I ${NSA_DEV} -t1 -b
	log_test_addr ${a} $? 0 "TCP socket bind to local address with device bind"

	# Sadly, the kernel allows binding a socket to a device and then
	# binding to an address not on the device. The only restriction
	# is that the address is valid in the L3 domain. So this test
	# passes when it really should not
	a=${VRF_IP6}
	log_start
	show_hint "Tecnically should fail since address is not on device but kernel allows"
	run_cmd nettest -6 -s -l ${a} -I ${NSA_DEV} -t1 -b
	log_test_addr ${a} $? 0 "TCP socket bind to VRF address with device bind"

	a=${NSA_LO_IP6}
	log_start
	show_hint "Address on loopback out of scope for VRF"
	run_cmd nettest -6 -s -l ${a} -I ${VRF} -t1 -b
	log_test_addr ${a} $? 1 "TCP socket bind to invalid local address for VRF"

	log_start
	show_hint "Address on loopback out of scope for device in VRF"
	run_cmd nettest -6 -s -l ${a} -I ${NSA_DEV} -t1 -b
	log_test_addr ${a} $? 1 "TCP socket bind to invalid local address for device bind"

}

ipv6_addr_bind()
{
	log_section "IPv6 address binds"

	log_subsection "No VRF"
	setup
	ipv6_addr_bind_novrf

	log_subsection "With VRF"
	setup "yes"
	ipv6_addr_bind_vrf
}

################################################################################
# IPv6 runtime tests

ipv6_rt()
{
	local desc="$1"
	local varg="-6 $2"
	local with_vrf="yes"
	local a

	#
	# server tests
	#
	for a in ${NSA_IP6} ${VRF_IP6}
	do
		log_start
		run_cmd nettest ${varg} -s &
		sleep 1
		run_cmd_nsb nettest ${varg} -r ${a} &
		sleep 3
		run_cmd ip link del ${VRF}
		sleep 1
		log_test_addr ${a} 0 0 "${desc}, global server"

		setup ${with_vrf}
	done

	for a in ${NSA_IP6} ${VRF_IP6}
	do
		log_start
		run_cmd nettest ${varg} -I ${VRF} -s &
		sleep 1
		run_cmd_nsb nettest ${varg} -r ${a} &
		sleep 3
		run_cmd ip link del ${VRF}
		sleep 1
		log_test_addr ${a} 0 0 "${desc}, VRF server"

		setup ${with_vrf}
	done

	for a in ${NSA_IP6} ${VRF_IP6}
	do
		log_start
		run_cmd nettest ${varg} -I ${NSA_DEV} -s &
		sleep 1
		run_cmd_nsb nettest ${varg} -r ${a} &
		sleep 3
		run_cmd ip link del ${VRF}
		sleep 1
		log_test_addr ${a} 0 0 "${desc}, enslaved device server"

		setup ${with_vrf}
	done

	#
	# client test
	#
	log_start
	run_cmd_nsb nettest ${varg} -s &
	sleep 1
	run_cmd nettest ${varg} -d ${VRF} -r ${NSB_IP6} &
	sleep 3
	run_cmd ip link del ${VRF}
	sleep 1
	log_test  0 0 "${desc}, VRF client"

	setup ${with_vrf}

	log_start
	run_cmd_nsb nettest ${varg} -s &
	sleep 1
	run_cmd nettest ${varg} -d ${NSA_DEV} -r ${NSB_IP6} &
	sleep 3
	run_cmd ip link del ${VRF}
	sleep 1
	log_test  0 0 "${desc}, enslaved device client"

	setup ${with_vrf}


	#
	# local address tests
	#
	for a in ${NSA_IP6} ${VRF_IP6}
	do
		log_start
		run_cmd nettest ${varg} -s &
		sleep 1
		run_cmd nettest ${varg} -d ${VRF} -r ${a} &
		sleep 3
		run_cmd ip link del ${VRF}
		sleep 1
		log_test_addr ${a} 0 0 "${desc}, global server, VRF client"

		setup ${with_vrf}
	done

	for a in ${NSA_IP6} ${VRF_IP6}
	do
		log_start
		run_cmd nettest ${varg} -I ${VRF} -s &
		sleep 1
		run_cmd nettest ${varg} -d ${VRF} -r ${a} &
		sleep 3
		run_cmd ip link del ${VRF}
		sleep 1
		log_test_addr ${a} 0 0 "${desc}, VRF server and client"

		setup ${with_vrf}
	done

	a=${NSA_IP6}
	log_start
	run_cmd nettest ${varg} -s &
	sleep 1
	run_cmd nettest ${varg} -d ${NSA_DEV} -r ${a} &
	sleep 3
	run_cmd ip link del ${VRF}
	sleep 1
	log_test_addr ${a} 0 0 "${desc}, global server, device client"

	setup ${with_vrf}

	log_start
	run_cmd nettest ${varg} -I ${VRF} -s &
	sleep 1
	run_cmd nettest ${varg} -d ${NSA_DEV} -r ${a} &
	sleep 3
	run_cmd ip link del ${VRF}
	sleep 1
	log_test_addr ${a} 0 0 "${desc}, VRF server, device client"

	setup ${with_vrf}

	log_start
	run_cmd nettest ${varg} -I ${NSA_DEV} -s &
	sleep 1
	run_cmd nettest ${varg} -d ${NSA_DEV} -r ${a} &
	sleep 3
	run_cmd ip link del ${VRF}
	sleep 1
	log_test_addr ${a} 0 0 "${desc}, device server, device client"
}

ipv6_ping_rt()
{
	local with_vrf="yes"
	local a

	a=${NSA_IP6}
	log_start
	run_cmd_nsb ${ping6} -f ${a} &
	sleep 3
	run_cmd ip link del ${VRF}
	sleep 1
	log_test_addr ${a} 0 0 "Device delete with active traffic - ping in"

	setup ${with_vrf}

	log_start
	run_cmd ${ping6} -f ${NSB_IP6} -I ${VRF} &
	sleep 1
	run_cmd ip link del ${VRF}
	sleep 1
	log_test_addr ${a} 0 0 "Device delete with active traffic - ping out"
}

ipv6_runtime()
{
	log_section "Run time tests - ipv6"

	setup "yes"
	ipv6_ping_rt

	setup "yes"
	ipv6_rt "TCP active socket"  "-n -1"

	setup "yes"
	ipv6_rt "TCP passive socket" "-i"

	setup "yes"
	ipv6_rt "UDP active socket"  "-D -n -1"
}

################################################################################
# netfilter blocking connections

netfilter_tcp_reset()
{
	local a

	for a in ${NSA_IP} ${VRF_IP}
	do
		log_start
		run_cmd nettest -s &
		sleep 1
		run_cmd_nsb nettest -r ${a}
		log_test_addr ${a} $? 1 "Global server, reject with TCP-reset on Rx"
	done
}

netfilter_icmp()
{
	local stype="$1"
	local arg
	local a

	[ "${stype}" = "UDP" ] && arg="-D"

	for a in ${NSA_IP} ${VRF_IP}
	do
		log_start
		run_cmd nettest ${arg} -s &
		sleep 1
		run_cmd_nsb nettest ${arg} -r ${a}
		log_test_addr ${a} $? 1 "Global ${stype} server, Rx reject icmp-port-unreach"
	done
}

ipv4_netfilter()
{
	log_section "IPv4 Netfilter"
	log_subsection "TCP reset"

	setup "yes"
	run_cmd iptables -A INPUT -p tcp --dport 12345 -j REJECT --reject-with tcp-reset

	netfilter_tcp_reset

	log_start
	log_subsection "ICMP unreachable"

	log_start
	run_cmd iptables -F
	run_cmd iptables -A INPUT -p tcp --dport 12345 -j REJECT --reject-with icmp-port-unreachable
	run_cmd iptables -A INPUT -p udp --dport 12345 -j REJECT --reject-with icmp-port-unreachable

	netfilter_icmp "TCP"
	netfilter_icmp "UDP"

	log_start
	iptables -F
}

netfilter_tcp6_reset()
{
	local a

	for a in ${NSA_IP6} ${VRF_IP6}
	do
		log_start
		run_cmd nettest -6 -s &
		sleep 1
		run_cmd_nsb nettest -6 -r ${a}
		log_test_addr ${a} $? 1 "Global server, reject with TCP-reset on Rx"
	done
}

netfilter_icmp6()
{
	local stype="$1"
	local arg
	local a

	[ "${stype}" = "UDP" ] && arg="$arg -D"

	for a in ${NSA_IP6} ${VRF_IP6}
	do
		log_start
		run_cmd nettest -6 -s ${arg} &
		sleep 1
		run_cmd_nsb nettest -6 ${arg} -r ${a}
		log_test_addr ${a} $? 1 "Global ${stype} server, Rx reject icmp-port-unreach"
	done
}

ipv6_netfilter()
{
	log_section "IPv6 Netfilter"
	log_subsection "TCP reset"

	setup "yes"
	run_cmd ip6tables -A INPUT -p tcp --dport 12345 -j REJECT --reject-with tcp-reset

	netfilter_tcp6_reset

	log_subsection "ICMP unreachable"

	log_start
	run_cmd ip6tables -F
	run_cmd ip6tables -A INPUT -p tcp --dport 12345 -j REJECT --reject-with icmp6-port-unreachable
	run_cmd ip6tables -A INPUT -p udp --dport 12345 -j REJECT --reject-with icmp6-port-unreachable

	netfilter_icmp6 "TCP"
	netfilter_icmp6 "UDP"

	log_start
	ip6tables -F
}

################################################################################
# specific use cases

# VRF only.
# ns-A device enslaved to bridge. Verify traffic with and without
# br_netfilter module loaded. Repeat with SVI on bridge.
use_case_br()
{
	setup "yes"

	setup_cmd ip link set ${NSA_DEV} down
	setup_cmd ip addr del dev ${NSA_DEV} ${NSA_IP}/24
	setup_cmd ip -6 addr del dev ${NSA_DEV} ${NSA_IP6}/64

	setup_cmd ip link add br0 type bridge
	setup_cmd ip addr add dev br0 ${NSA_IP}/24
	setup_cmd ip -6 addr add dev br0 ${NSA_IP6}/64 nodad

	setup_cmd ip li set ${NSA_DEV} master br0
	setup_cmd ip li set ${NSA_DEV} up
	setup_cmd ip li set br0 up
	setup_cmd ip li set br0 vrf ${VRF}

	rmmod br_netfilter 2>/dev/null
	sleep 5 # DAD

	run_cmd ip neigh flush all
	run_cmd ping -c1 -w1 -I br0 ${NSB_IP}
	log_test $? 0 "Bridge into VRF - IPv4 ping out"

	run_cmd ip neigh flush all
	run_cmd ${ping6} -c1 -w1 -I br0 ${NSB_IP6}
	log_test $? 0 "Bridge into VRF - IPv6 ping out"

	run_cmd ip neigh flush all
	run_cmd_nsb ping -c1 -w1 ${NSA_IP}
	log_test $? 0 "Bridge into VRF - IPv4 ping in"

	run_cmd ip neigh flush all
	run_cmd_nsb ${ping6} -c1 -w1 ${NSA_IP6}
	log_test $? 0 "Bridge into VRF - IPv6 ping in"

	modprobe br_netfilter
	if [ $? -eq 0 ]; then
		run_cmd ip neigh flush all
		run_cmd ping -c1 -w1 -I br0 ${NSB_IP}
		log_test $? 0 "Bridge into VRF with br_netfilter - IPv4 ping out"

		run_cmd ip neigh flush all
		run_cmd ${ping6} -c1 -w1 -I br0 ${NSB_IP6}
		log_test $? 0 "Bridge into VRF with br_netfilter - IPv6 ping out"

		run_cmd ip neigh flush all
		run_cmd_nsb ping -c1 -w1 ${NSA_IP}
		log_test $? 0 "Bridge into VRF with br_netfilter - IPv4 ping in"

		run_cmd ip neigh flush all
		run_cmd_nsb ${ping6} -c1 -w1 ${NSA_IP6}
		log_test $? 0 "Bridge into VRF with br_netfilter - IPv6 ping in"
	fi

	setup_cmd ip li set br0 nomaster
	setup_cmd ip li add br0.100 link br0 type vlan id 100
	setup_cmd ip li set br0.100 vrf ${VRF} up
	setup_cmd ip    addr add dev br0.100 172.16.101.1/24
	setup_cmd ip -6 addr add dev br0.100 2001:db8:101::1/64 nodad

	setup_cmd_nsb ip li add vlan100 link ${NSB_DEV} type vlan id 100
	setup_cmd_nsb ip addr add dev vlan100 172.16.101.2/24
	setup_cmd_nsb ip -6 addr add dev vlan100 2001:db8:101::2/64 nodad
	setup_cmd_nsb ip li set vlan100 up
	sleep 1

	rmmod br_netfilter 2>/dev/null

	run_cmd ip neigh flush all
	run_cmd ping -c1 -w1 -I br0.100 172.16.101.2
	log_test $? 0 "Bridge vlan into VRF - IPv4 ping out"

	run_cmd ip neigh flush all
	run_cmd ${ping6} -c1 -w1 -I br0.100 2001:db8:101::2
	log_test $? 0 "Bridge vlan into VRF - IPv6 ping out"

	run_cmd ip neigh flush all
	run_cmd_nsb ping -c1 -w1 172.16.101.1
	log_test $? 0 "Bridge vlan into VRF - IPv4 ping in"

	run_cmd ip neigh flush all
	run_cmd_nsb ${ping6} -c1 -w1 2001:db8:101::1
	log_test $? 0 "Bridge vlan into VRF - IPv6 ping in"

	modprobe br_netfilter
	if [ $? -eq 0 ]; then
		run_cmd ip neigh flush all
		run_cmd ping -c1 -w1 -I br0.100 172.16.101.2
		log_test $? 0 "Bridge vlan into VRF with br_netfilter - IPv4 ping out"

		run_cmd ip neigh flush all
		run_cmd ${ping6} -c1 -w1 -I br0.100 2001:db8:101::2
		log_test $? 0 "Bridge vlan into VRF with br_netfilter - IPv6 ping out"

		run_cmd ip neigh flush all
		run_cmd_nsb ping -c1 -w1 172.16.101.1
		log_test $? 0 "Bridge vlan into VRF - IPv4 ping in"

		run_cmd ip neigh flush all
		run_cmd_nsb ${ping6} -c1 -w1 2001:db8:101::1
		log_test $? 0 "Bridge vlan into VRF - IPv6 ping in"
	fi

	setup_cmd ip li del br0 2>/dev/null
	setup_cmd_nsb ip li del vlan100 2>/dev/null
}

# VRF only.
# ns-A device is connected to both ns-B and ns-C on a single VRF but only has
# LLA on the interfaces
use_case_ping_lla_multi()
{
	setup_lla_only
	# only want reply from ns-A
	setup_cmd_nsb sysctl -qw net.ipv6.icmp.echo_ignore_multicast=1
	setup_cmd_nsc sysctl -qw net.ipv6.icmp.echo_ignore_multicast=1

	log_start
	run_cmd_nsb ping -c1 -w1 ${MCAST}%${NSB_DEV}
	log_test_addr ${MCAST}%${NSB_DEV} $? 0 "Pre cycle, ping out ns-B"

	run_cmd_nsc ping -c1 -w1 ${MCAST}%${NSC_DEV}
	log_test_addr ${MCAST}%${NSC_DEV} $? 0 "Pre cycle, ping out ns-C"

	# cycle/flap the first ns-A interface
	setup_cmd ip link set ${NSA_DEV} down
	setup_cmd ip link set ${NSA_DEV} up
	sleep 1

	log_start
	run_cmd_nsb ping -c1 -w1 ${MCAST}%${NSB_DEV}
	log_test_addr ${MCAST}%${NSB_DEV} $? 0 "Post cycle ${NSA} ${NSA_DEV}, ping out ns-B"
	run_cmd_nsc ping -c1 -w1 ${MCAST}%${NSC_DEV}
	log_test_addr ${MCAST}%${NSC_DEV} $? 0 "Post cycle ${NSA} ${NSA_DEV}, ping out ns-C"

	# cycle/flap the second ns-A interface
	setup_cmd ip link set ${NSA_DEV2} down
	setup_cmd ip link set ${NSA_DEV2} up
	sleep 1

	log_start
	run_cmd_nsb ping -c1 -w1 ${MCAST}%${NSB_DEV}
	log_test_addr ${MCAST}%${NSB_DEV} $? 0 "Post cycle ${NSA} ${NSA_DEV2}, ping out ns-B"
	run_cmd_nsc ping -c1 -w1 ${MCAST}%${NSC_DEV}
	log_test_addr ${MCAST}%${NSC_DEV} $? 0 "Post cycle ${NSA} ${NSA_DEV2}, ping out ns-C"
}

# Perform IPv{4,6} SNAT on ns-A, and verify TCP connection is successfully
# established with ns-B.
use_case_snat_on_vrf()
{
	setup "yes"

	local port="12345"

	run_cmd iptables -t nat -A POSTROUTING -p tcp -m tcp --dport ${port} -j SNAT --to-source ${NSA_LO_IP} -o ${VRF}
	run_cmd ip6tables -t nat -A POSTROUTING -p tcp -m tcp --dport ${port} -j SNAT --to-source ${NSA_LO_IP6} -o ${VRF}

	run_cmd_nsb nettest -s -l ${NSB_IP} -p ${port} &
	sleep 1
	run_cmd nettest -d ${VRF} -r ${NSB_IP} -p ${port}
	log_test $? 0 "IPv4 TCP connection over VRF with SNAT"

	run_cmd_nsb nettest -6 -s -l ${NSB_IP6} -p ${port} &
	sleep 1
	run_cmd nettest -6 -d ${VRF} -r ${NSB_IP6} -p ${port}
	log_test $? 0 "IPv6 TCP connection over VRF with SNAT"

	# Cleanup
	run_cmd iptables -t nat -D POSTROUTING -p tcp -m tcp --dport ${port} -j SNAT --to-source ${NSA_LO_IP} -o ${VRF}
	run_cmd ip6tables -t nat -D POSTROUTING -p tcp -m tcp --dport ${port} -j SNAT --to-source ${NSA_LO_IP6} -o ${VRF}
}

use_cases()
{
	log_section "Use cases"
	log_subsection "Device enslaved to bridge"
	use_case_br
	log_subsection "Ping LLA with multiple interfaces"
	use_case_ping_lla_multi
	log_subsection "SNAT on VRF"
	use_case_snat_on_vrf
}

################################################################################
# usage

usage()
{
	cat <<EOF
usage: ${0##*/} OPTS

	-4          IPv4 tests only
	-6          IPv6 tests only
	-t <test>   Test name/set to run
	-p          Pause on fail
	-P          Pause after each test
	-v          Be verbose
EOF
}

################################################################################
# main

TESTS_IPV4="ipv4_ping ipv4_tcp ipv4_udp ipv4_bind ipv4_runtime ipv4_netfilter"
TESTS_IPV6="ipv6_ping ipv6_tcp ipv6_udp ipv6_bind ipv6_runtime ipv6_netfilter"
TESTS_OTHER="use_cases"

PAUSE_ON_FAIL=no
PAUSE=no

while getopts :46t:pPvh o
do
	case $o in
		4) TESTS=ipv4;;
		6) TESTS=ipv6;;
		t) TESTS=$OPTARG;;
		p) PAUSE_ON_FAIL=yes;;
		P) PAUSE=yes;;
		v) VERBOSE=1;;
		h) usage; exit 0;;
		*) usage; exit 1;;
	esac
done

# make sure we don't pause twice
[ "${PAUSE}" = "yes" ] && PAUSE_ON_FAIL=no

#
# show user test config
#
if [ -z "$TESTS" ]; then
	TESTS="$TESTS_IPV4 $TESTS_IPV6 $TESTS_OTHER"
elif [ "$TESTS" = "ipv4" ]; then
	TESTS="$TESTS_IPV4"
elif [ "$TESTS" = "ipv6" ]; then
	TESTS="$TESTS_IPV6"
fi

which nettest >/dev/null
if [ $? -ne 0 ]; then
	echo "'nettest' command not found; skipping tests"
	exit $ksft_skip
fi

declare -i nfail=0
declare -i nsuccess=0

for t in $TESTS
do
	case $t in
	ipv4_ping|ping)  ipv4_ping;;
	ipv4_tcp|tcp)    ipv4_tcp;;
	ipv4_udp|udp)    ipv4_udp;;
	ipv4_bind|bind)  ipv4_addr_bind;;
	ipv4_runtime)    ipv4_runtime;;
	ipv4_netfilter)  ipv4_netfilter;;

	ipv6_ping|ping6) ipv6_ping;;
	ipv6_tcp|tcp6)   ipv6_tcp;;
	ipv6_udp|udp6)   ipv6_udp;;
	ipv6_bind|bind6) ipv6_addr_bind;;
	ipv6_runtime)    ipv6_runtime;;
	ipv6_netfilter)  ipv6_netfilter;;

	use_cases)       use_cases;;

	# setup namespaces and config, but do not run any tests
	setup)		 setup; exit 0;;
	vrf_setup)	 setup "yes"; exit 0;;

	help)            echo "Test names: $TESTS"; exit 0;;
	esac
done

cleanup 2>/dev/null

printf "\nTests passed: %3d\n" ${nsuccess}
printf "Tests failed: %3d\n"   ${nfail}
