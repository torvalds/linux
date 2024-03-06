#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# ns: me               | ns: peer              | ns: remote
#   2001:db8:91::1     |       2001:db8:91::2  |
#   172.16.1.1         |       172.16.1.2      |
#            veth1 <---|---> veth2             |
#                      |              veth5 <--|--> veth6  172.16.101.1
#            veth3 <---|---> veth4             |           2001:db8:101::1
#   172.16.2.1         |       172.16.2.2      |
#   2001:db8:92::1     |       2001:db8:92::2  |
#
# This test is for checking IPv4 and IPv6 FIB behavior with nexthop
# objects. Device reference counts and network namespace cleanup tested
# by use of network namespace for peer.

source lib.sh
ret=0
# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

# all tests in this script. Can be overridden with -t option
IPV4_TESTS="
	ipv4_fcnal
	ipv4_grp_fcnal
	ipv4_res_grp_fcnal
	ipv4_withv6_fcnal
	ipv4_fcnal_runtime
	ipv4_large_grp
	ipv4_large_res_grp
	ipv4_compat_mode
	ipv4_fdb_grp_fcnal
	ipv4_mpath_select
	ipv4_torture
	ipv4_res_torture
"

IPV6_TESTS="
	ipv6_fcnal
	ipv6_grp_fcnal
	ipv6_res_grp_fcnal
	ipv6_fcnal_runtime
	ipv6_large_grp
	ipv6_large_res_grp
	ipv6_compat_mode
	ipv6_fdb_grp_fcnal
	ipv6_mpath_select
	ipv6_torture
	ipv6_res_torture
"

ALL_TESTS="
	basic
	basic_res
	${IPV4_TESTS}
	${IPV6_TESTS}
"
TESTS="${ALL_TESTS}"
VERBOSE=0
PAUSE_ON_FAIL=no
PAUSE=no
PING_TIMEOUT=5

nsid=100

################################################################################
# utilities

log_test()
{
	local rc=$1
	local expected=$2
	local msg="$3"

	if [ ${rc} -eq ${expected} ]; then
		printf "TEST: %-60s  [ OK ]\n" "${msg}"
		nsuccess=$((nsuccess+1))
	else
		ret=1
		nfail=$((nfail+1))
		printf "TEST: %-60s  [FAIL]\n" "${msg}"
		if [ "$VERBOSE" = "1" ]; then
			echo "    rc=$rc, expected $expected"
		fi

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

	[ "$VERBOSE" = "1" ] && echo
}

run_cmd()
{
	local cmd="$1"
	local out
	local stderr="2>/dev/null"

	if [ "$VERBOSE" = "1" ]; then
		printf "COMMAND: $cmd\n"
		stderr=
	fi

	out=$(eval $cmd $stderr)
	rc=$?
	if [ "$VERBOSE" = "1" -a -n "$out" ]; then
		echo "    $out"
	fi

	return $rc
}

get_linklocal()
{
	local dev=$1
	local ns
	local addr

	[ -n "$2" ] && ns="-netns $2"
	addr=$(ip $ns -6 -br addr show dev ${dev} | \
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

create_ns()
{
	local n=${1}

	set -e

	ip netns exec ${n} sysctl -qw net.ipv4.ip_forward=1
	ip netns exec ${n} sysctl -qw net.ipv4.fib_multipath_use_neigh=1
	ip netns exec ${n} sysctl -qw net.ipv4.conf.default.ignore_routes_with_linkdown=1
	ip netns exec ${n} sysctl -qw net.ipv6.conf.all.keep_addr_on_down=1
	ip netns exec ${n} sysctl -qw net.ipv6.conf.all.forwarding=1
	ip netns exec ${n} sysctl -qw net.ipv6.conf.default.forwarding=1
	ip netns exec ${n} sysctl -qw net.ipv6.conf.default.ignore_routes_with_linkdown=1
	ip netns exec ${n} sysctl -qw net.ipv6.conf.all.accept_dad=0
	ip netns exec ${n} sysctl -qw net.ipv6.conf.default.accept_dad=0

	set +e
}

setup()
{
	cleanup

	setup_ns me peer remote
	create_ns $me
	create_ns $peer
	create_ns $remote

	IP="ip -netns $me"
	BRIDGE="bridge -netns $me"
	set -e
	$IP li add veth1 type veth peer name veth2
	$IP li set veth1 up
	$IP addr add 172.16.1.1/24 dev veth1
	$IP -6 addr add 2001:db8:91::1/64 dev veth1 nodad

	$IP li add veth3 type veth peer name veth4
	$IP li set veth3 up
	$IP addr add 172.16.2.1/24 dev veth3
	$IP -6 addr add 2001:db8:92::1/64 dev veth3 nodad

	$IP li set veth2 netns $peer up
	ip -netns $peer addr add 172.16.1.2/24 dev veth2
	ip -netns $peer -6 addr add 2001:db8:91::2/64 dev veth2 nodad

	$IP li set veth4 netns $peer up
	ip -netns $peer addr add 172.16.2.2/24 dev veth4
	ip -netns $peer -6 addr add 2001:db8:92::2/64 dev veth4 nodad

	ip -netns $remote li add veth5 type veth peer name veth6
	ip -netns $remote li set veth5 up
	ip -netns $remote addr add dev veth5 172.16.101.1/24
	ip -netns $remote -6 addr add dev veth5 2001:db8:101::1/64 nodad
	ip -netns $remote ro add 172.16.0.0/22 via 172.16.101.2
	ip -netns $remote -6 ro add 2001:db8:90::/40 via 2001:db8:101::2

	ip -netns $remote li set veth6 netns $peer up
	ip -netns $peer addr add dev veth6 172.16.101.2/24
	ip -netns $peer -6 addr add dev veth6 2001:db8:101::2/64 nodad
	set +e
}

cleanup()
{
	local ns

	for ns in $me $peer $remote; do
		ip netns del ${ns} 2>/dev/null
	done
}

check_output()
{
	local out="$1"
	local expected="$2"
	local rc=0

	[ "${out}" = "${expected}" ] && return 0

	if [ -z "${out}" ]; then
		if [ "$VERBOSE" = "1" ]; then
			printf "\nNo entry found\n"
			printf "Expected:\n"
			printf "    ${expected}\n"
		fi
		return 1
	fi

	out=$(echo ${out})
	if [ "${out}" != "${expected}" ]; then
		rc=1
		if [ "${VERBOSE}" = "1" ]; then
			printf "    Unexpected entry. Have:\n"
			printf "        ${out}\n"
			printf "    Expected:\n"
			printf "        ${expected}\n\n"
		else
			echo "      WARNING: Unexpected route entry"
		fi
	fi

	return $rc
}

check_nexthop()
{
	local nharg="$1"
	local expected="$2"
	local out

	out=$($IP nexthop ls ${nharg} 2>/dev/null)

	check_output "${out}" "${expected}"
}

check_nexthop_bucket()
{
	local nharg="$1"
	local expected="$2"
	local out

	# remove the idle time since we cannot match it
	out=$($IP nexthop bucket ${nharg} \
		| sed s/idle_time\ [0-9.]*\ // 2>/dev/null)

	check_output "${out}" "${expected}"
}

check_route()
{
	local pfx="$1"
	local expected="$2"
	local out

	out=$($IP route ls match ${pfx} 2>/dev/null)

	check_output "${out}" "${expected}"
}

check_route6()
{
	local pfx="$1"
	local expected="$2"
	local out

	out=$($IP -6 route ls match ${pfx} 2>/dev/null | sed -e 's/pref medium//')

	check_output "${out}" "${expected}"
}

check_large_grp()
{
	local ipv=$1
	local ecmp=$2
	local grpnum=100
	local nhidstart=100
	local grpidstart=1000
	local iter=0
	local nhidstr=""
	local grpidstr=""
	local grpstr=""
	local ipstr=""

	if [ $ipv -eq 4 ]; then
		ipstr="172.16.1."
	else
		ipstr="2001:db8:91::"
	fi

	#
	# Create $grpnum groups with specified $ecmp and dump them
	#

	# create nexthops with different gateways
	iter=2
	while [ $iter -le $(($ecmp + 1)) ]
	do
		nhidstr="$(($nhidstart + $iter))"
		run_cmd "$IP nexthop add id $nhidstr via $ipstr$iter dev veth1"
		check_nexthop "id $nhidstr" "id $nhidstr via $ipstr$iter dev veth1 scope link"

		if [ $iter -le $ecmp ]; then
			grpstr+="$nhidstr/"
		else
			grpstr+="$nhidstr"
		fi
		((iter++))
	done

	# create duplicate large ecmp groups
	iter=0
	while [ $iter -le $grpnum ]
	do
		grpidstr="$(($grpidstart + $iter))"
		run_cmd "$IP nexthop add id $grpidstr group $grpstr"
		check_nexthop "id $grpidstr" "id $grpidstr group $grpstr"
		((iter++))
	done

	# dump large groups
	run_cmd "$IP nexthop list"
	log_test $? 0 "Dump large (x$ecmp) ecmp groups"
}

check_large_res_grp()
{
	local ipv=$1
	local buckets=$2
	local ipstr=""

	if [ $ipv -eq 4 ]; then
		ipstr="172.16.1.2"
	else
		ipstr="2001:db8:91::2"
	fi

	# create a resilient group with $buckets buckets and dump them
	run_cmd "$IP nexthop add id 100 via $ipstr dev veth1"
	run_cmd "$IP nexthop add id 1000 group 100 type resilient buckets $buckets"
	run_cmd "$IP nexthop bucket list"
	log_test $? 0 "Dump large (x$buckets) nexthop buckets"
}

get_route_dev()
{
	local pfx="$1"
	local out

	if out=$($IP -j route get "$pfx" | jq -re ".[0].dev"); then
		echo "$out"
	fi
}

check_route_dev()
{
	local pfx="$1"
	local expected="$2"
	local out

	out=$(get_route_dev "$pfx")

	check_output "$out" "$expected"
}

start_ip_monitor()
{
	local mtype=$1

	# start the monitor in the background
	tmpfile=`mktemp /var/run/nexthoptestXXX`
	mpid=`($IP monitor $mtype > $tmpfile & echo $!) 2>/dev/null`
	sleep 0.2
	echo "$mpid $tmpfile"
}

stop_ip_monitor()
{
	local mpid=$1
	local tmpfile=$2
	local el=$3

	# check the monitor results
	kill $mpid
	lines=`wc -l $tmpfile | cut "-d " -f1`
	test $lines -eq $el
	rc=$?
	rm -rf $tmpfile

	return $rc
}

check_nexthop_fdb_support()
{
	$IP nexthop help 2>&1 | grep -q fdb
	if [ $? -ne 0 ]; then
		echo "SKIP: iproute2 too old, missing fdb nexthop support"
		return $ksft_skip
	fi
}

check_nexthop_res_support()
{
	$IP nexthop help 2>&1 | grep -q resilient
	if [ $? -ne 0 ]; then
		echo "SKIP: iproute2 too old, missing resilient nexthop group support"
		return $ksft_skip
	fi
}

ipv6_fdb_grp_fcnal()
{
	local rc

	echo
	echo "IPv6 fdb groups functional"
	echo "--------------------------"

	check_nexthop_fdb_support
	if [ $? -eq $ksft_skip ]; then
		return $ksft_skip
	fi

	# create group with multiple nexthops
	run_cmd "$IP nexthop add id 61 via 2001:db8:91::2 fdb"
	run_cmd "$IP nexthop add id 62 via 2001:db8:91::3 fdb"
	run_cmd "$IP nexthop add id 102 group 61/62 fdb"
	check_nexthop "id 102" "id 102 group 61/62 fdb"
	log_test $? 0 "Fdb Nexthop group with multiple nexthops"

	## get nexthop group
	run_cmd "$IP nexthop get id 102"
	check_nexthop "id 102" "id 102 group 61/62 fdb"
	log_test $? 0 "Get Fdb nexthop group by id"

	# fdb nexthop group can only contain fdb nexthops
	run_cmd "$IP nexthop add id 63 via 2001:db8:91::4"
	run_cmd "$IP nexthop add id 64 via 2001:db8:91::5"
	run_cmd "$IP nexthop add id 103 group 63/64 fdb"
	log_test $? 2 "Fdb Nexthop group with non-fdb nexthops"

	# Non fdb nexthop group can not contain fdb nexthops
	run_cmd "$IP nexthop add id 65 via 2001:db8:91::5 fdb"
	run_cmd "$IP nexthop add id 66 via 2001:db8:91::6 fdb"
	run_cmd "$IP nexthop add id 104 group 65/66"
	log_test $? 2 "Non-Fdb Nexthop group with fdb nexthops"

	# fdb nexthop cannot have blackhole
	run_cmd "$IP nexthop add id 67 blackhole fdb"
	log_test $? 2 "Fdb Nexthop with blackhole"

	# fdb nexthop with oif
	run_cmd "$IP nexthop add id 68 via 2001:db8:91::7 dev veth1 fdb"
	log_test $? 2 "Fdb Nexthop with oif"

	# fdb nexthop with onlink
	run_cmd "$IP nexthop add id 68 via 2001:db8:91::7 onlink fdb"
	log_test $? 2 "Fdb Nexthop with onlink"

	# fdb nexthop with encap
	run_cmd "$IP nexthop add id 69 encap mpls 101 via 2001:db8:91::8 dev veth1 fdb"
	log_test $? 2 "Fdb Nexthop with encap"

	run_cmd "$IP link add name vx10 type vxlan id 1010 local 2001:db8:91::9 remote 2001:db8:91::10 dstport 4789 nolearning noudpcsum tos inherit ttl 100"
	run_cmd "$BRIDGE fdb add 02:02:00:00:00:13 dev vx10 nhid 102 self"
	log_test $? 0 "Fdb mac add with nexthop group"

	## fdb nexthops can only reference nexthop groups and not nexthops
	run_cmd "$BRIDGE fdb add 02:02:00:00:00:14 dev vx10 nhid 61 self"
	log_test $? 255 "Fdb mac add with nexthop"

	run_cmd "$IP -6 ro add 2001:db8:101::1/128 nhid 66"
	log_test $? 2 "Route add with fdb nexthop"

	run_cmd "$IP -6 ro add 2001:db8:101::1/128 nhid 103"
	log_test $? 2 "Route add with fdb nexthop group"

	run_cmd "$IP nexthop del id 61"
	run_cmd "$BRIDGE fdb get to 02:02:00:00:00:13 dev vx10 self"
	log_test $? 0 "Fdb entry after deleting a single nexthop"

	run_cmd "$IP nexthop del id 102"
	log_test $? 0 "Fdb nexthop delete"

	run_cmd "$BRIDGE fdb get to 02:02:00:00:00:13 dev vx10 self"
	log_test $? 254 "Fdb entry after deleting a nexthop group"

	$IP link del dev vx10
}

ipv4_fdb_grp_fcnal()
{
	local rc

	echo
	echo "IPv4 fdb groups functional"
	echo "--------------------------"

	check_nexthop_fdb_support
	if [ $? -eq $ksft_skip ]; then
		return $ksft_skip
	fi

	# create group with multiple nexthops
	run_cmd "$IP nexthop add id 12 via 172.16.1.2 fdb"
	run_cmd "$IP nexthop add id 13 via 172.16.1.3 fdb"
	run_cmd "$IP nexthop add id 102 group 12/13 fdb"
	check_nexthop "id 102" "id 102 group 12/13 fdb"
	log_test $? 0 "Fdb Nexthop group with multiple nexthops"

	# get nexthop group
	run_cmd "$IP nexthop get id 102"
	check_nexthop "id 102" "id 102 group 12/13 fdb"
	log_test $? 0 "Get Fdb nexthop group by id"

	# fdb nexthop group can only contain fdb nexthops
	run_cmd "$IP nexthop add id 14 via 172.16.1.2"
	run_cmd "$IP nexthop add id 15 via 172.16.1.3"
	run_cmd "$IP nexthop add id 103 group 14/15 fdb"
	log_test $? 2 "Fdb Nexthop group with non-fdb nexthops"

	# Non fdb nexthop group can not contain fdb nexthops
	run_cmd "$IP nexthop add id 16 via 172.16.1.2 fdb"
	run_cmd "$IP nexthop add id 17 via 172.16.1.3 fdb"
	run_cmd "$IP nexthop add id 104 group 14/15"
	log_test $? 2 "Non-Fdb Nexthop group with fdb nexthops"

	# fdb nexthop cannot have blackhole
	run_cmd "$IP nexthop add id 18 blackhole fdb"
	log_test $? 2 "Fdb Nexthop with blackhole"

	# fdb nexthop with oif
	run_cmd "$IP nexthop add id 16 via 172.16.1.2 dev veth1 fdb"
	log_test $? 2 "Fdb Nexthop with oif"

	# fdb nexthop with onlink
	run_cmd "$IP nexthop add id 16 via 172.16.1.2 onlink fdb"
	log_test $? 2 "Fdb Nexthop with onlink"

	# fdb nexthop with encap
	run_cmd "$IP nexthop add id 17 encap mpls 101 via 172.16.1.2 dev veth1 fdb"
	log_test $? 2 "Fdb Nexthop with encap"

	run_cmd "$IP link add name vx10 type vxlan id 1010 local 10.0.0.1 remote 10.0.0.2 dstport 4789 nolearning noudpcsum tos inherit ttl 100"
	run_cmd "$BRIDGE fdb add 02:02:00:00:00:13 dev vx10 nhid 102 self"
	log_test $? 0 "Fdb mac add with nexthop group"

	# fdb nexthops can only reference nexthop groups and not nexthops
	run_cmd "$BRIDGE fdb add 02:02:00:00:00:14 dev vx10 nhid 12 self"
	log_test $? 255 "Fdb mac add with nexthop"

	run_cmd "$IP ro add 172.16.0.0/22 nhid 15"
	log_test $? 2 "Route add with fdb nexthop"

	run_cmd "$IP ro add 172.16.0.0/22 nhid 103"
	log_test $? 2 "Route add with fdb nexthop group"

	run_cmd "$IP nexthop del id 12"
	run_cmd "$BRIDGE fdb get to 02:02:00:00:00:13 dev vx10 self"
	log_test $? 0 "Fdb entry after deleting a single nexthop"

	run_cmd "$IP nexthop del id 102"
	log_test $? 0 "Fdb nexthop delete"

	run_cmd "$BRIDGE fdb get to 02:02:00:00:00:13 dev vx10 self"
	log_test $? 254 "Fdb entry after deleting a nexthop group"

	$IP link del dev vx10
}

ipv4_mpath_select()
{
	local rc dev match h addr

	echo
	echo "IPv4 multipath selection"
	echo "------------------------"
	if [ ! -x "$(command -v jq)" ]; then
		echo "SKIP: Could not run test; need jq tool"
		return $ksft_skip
	fi

	# Use status of existing neighbor entry when determining nexthop for
	# multipath routes.
	local -A gws
	gws=([veth1]=172.16.1.2 [veth3]=172.16.2.2)
	local -A other_dev
	other_dev=([veth1]=veth3 [veth3]=veth1)

	run_cmd "$IP nexthop add id 1 via ${gws["veth1"]} dev veth1"
	run_cmd "$IP nexthop add id 2 via ${gws["veth3"]} dev veth3"
	run_cmd "$IP nexthop add id 1001 group 1/2"
	run_cmd "$IP ro add 172.16.101.0/24 nhid 1001"
	rc=0
	for dev in veth1 veth3; do
		match=0
		for h in {1..254}; do
			addr="172.16.101.$h"
			if [ "$(get_route_dev "$addr")" = "$dev" ]; then
				match=1
				break
			fi
		done
		if (( match == 0 )); then
			echo "SKIP: Did not find a route using device $dev"
			return $ksft_skip
		fi
		run_cmd "$IP neigh add ${gws[$dev]} dev $dev nud failed"
		if ! check_route_dev "$addr" "${other_dev[$dev]}"; then
			rc=1
			break
		fi
		run_cmd "$IP neigh del ${gws[$dev]} dev $dev"
	done
	log_test $rc 0 "Use valid neighbor during multipath selection"

	run_cmd "$IP neigh add 172.16.1.2 dev veth1 nud incomplete"
	run_cmd "$IP neigh add 172.16.2.2 dev veth3 nud incomplete"
	run_cmd "$IP route get 172.16.101.1"
	# if we did not crash, success
	log_test $rc 0 "Multipath selection with no valid neighbor"
}

ipv6_mpath_select()
{
	local rc dev match h addr

	echo
	echo "IPv6 multipath selection"
	echo "------------------------"
	if [ ! -x "$(command -v jq)" ]; then
		echo "SKIP: Could not run test; need jq tool"
		return $ksft_skip
	fi

	# Use status of existing neighbor entry when determining nexthop for
	# multipath routes.
	local -A gws
	gws=([veth1]=2001:db8:91::2 [veth3]=2001:db8:92::2)
	local -A other_dev
	other_dev=([veth1]=veth3 [veth3]=veth1)

	run_cmd "$IP nexthop add id 1 via ${gws["veth1"]} dev veth1"
	run_cmd "$IP nexthop add id 2 via ${gws["veth3"]} dev veth3"
	run_cmd "$IP nexthop add id 1001 group 1/2"
	run_cmd "$IP ro add 2001:db8:101::/64 nhid 1001"
	rc=0
	for dev in veth1 veth3; do
		match=0
		for h in {1..65535}; do
			addr=$(printf "2001:db8:101::%x" $h)
			if [ "$(get_route_dev "$addr")" = "$dev" ]; then
				match=1
				break
			fi
		done
		if (( match == 0 )); then
			echo "SKIP: Did not find a route using device $dev"
			return $ksft_skip
		fi
		run_cmd "$IP neigh add ${gws[$dev]} dev $dev nud failed"
		if ! check_route_dev "$addr" "${other_dev[$dev]}"; then
			rc=1
			break
		fi
		run_cmd "$IP neigh del ${gws[$dev]} dev $dev"
	done
	log_test $rc 0 "Use valid neighbor during multipath selection"

	run_cmd "$IP neigh add 2001:db8:91::2 dev veth1 nud incomplete"
	run_cmd "$IP neigh add 2001:db8:92::2 dev veth3 nud incomplete"
	run_cmd "$IP route get 2001:db8:101::1"
	# if we did not crash, success
	log_test $rc 0 "Multipath selection with no valid neighbor"
}

################################################################################
# basic operations (add, delete, replace) on nexthops and nexthop groups
#
# IPv6

ipv6_fcnal()
{
	local rc

	echo
	echo "IPv6"
	echo "----------------------"

	run_cmd "$IP nexthop add id 52 via 2001:db8:91::2 dev veth1"
	rc=$?
	log_test $rc 0 "Create nexthop with id, gw, dev"
	if [ $rc -ne 0 ]; then
		echo "Basic IPv6 create fails; can not continue"
		return 1
	fi

	run_cmd "$IP nexthop get id 52"
	log_test $? 0 "Get nexthop by id"
	check_nexthop "id 52" "id 52 via 2001:db8:91::2 dev veth1 scope link"

	run_cmd "$IP nexthop del id 52"
	log_test $? 0 "Delete nexthop by id"
	check_nexthop "id 52" ""

	#
	# gw, device spec
	#
	# gw validation, no device - fails since dev required
	run_cmd "$IP nexthop add id 52 via 2001:db8:92::3"
	log_test $? 2 "Create nexthop - gw only"

	# gw is not reachable throught given dev
	run_cmd "$IP nexthop add id 53 via 2001:db8:3::3 dev veth1"
	log_test $? 2 "Create nexthop - invalid gw+dev combination"

	# onlink arg overrides gw+dev lookup
	run_cmd "$IP nexthop add id 53 via 2001:db8:3::3 dev veth1 onlink"
	log_test $? 0 "Create nexthop - gw+dev and onlink"

	# admin down should delete nexthops
	set -e
	run_cmd "$IP -6 nexthop add id 55 via 2001:db8:91::3 dev veth1"
	run_cmd "$IP nexthop add id 56 via 2001:db8:91::4 dev veth1"
	run_cmd "$IP nexthop add id 57 via 2001:db8:91::5 dev veth1"
	run_cmd "$IP li set dev veth1 down"
	set +e
	check_nexthop "dev veth1" ""
	log_test $? 0 "Nexthops removed on admin down"
}

ipv6_grp_refs()
{
	if [ ! -x "$(command -v mausezahn)" ]; then
		echo "SKIP: Could not run test; need mausezahn tool"
		return
	fi

	run_cmd "$IP link set dev veth1 up"
	run_cmd "$IP link add veth1.10 link veth1 up type vlan id 10"
	run_cmd "$IP link add veth1.20 link veth1 up type vlan id 20"
	run_cmd "$IP -6 addr add 2001:db8:91::1/64 dev veth1.10"
	run_cmd "$IP -6 addr add 2001:db8:92::1/64 dev veth1.20"
	run_cmd "$IP -6 neigh add 2001:db8:91::2 lladdr 00:11:22:33:44:55 dev veth1.10"
	run_cmd "$IP -6 neigh add 2001:db8:92::2 lladdr 00:11:22:33:44:55 dev veth1.20"
	run_cmd "$IP nexthop add id 100 via 2001:db8:91::2 dev veth1.10"
	run_cmd "$IP nexthop add id 101 via 2001:db8:92::2 dev veth1.20"
	run_cmd "$IP nexthop add id 102 group 100"
	run_cmd "$IP route add 2001:db8:101::1/128 nhid 102"

	# create per-cpu dsts through nh 100
	run_cmd "ip netns exec $me mausezahn -6 veth1.10 -B 2001:db8:101::1 -A 2001:db8:91::1 -c 5 -t tcp "dp=1-1023, flags=syn" >/dev/null 2>&1"

	# remove nh 100 from the group to delete the route potentially leaving
	# a stale per-cpu dst which holds a reference to the nexthop's net
	# device and to the IPv6 route
	run_cmd "$IP nexthop replace id 102 group 101"
	run_cmd "$IP route del 2001:db8:101::1/128"

	# add both nexthops to the group so a reference is taken on them
	run_cmd "$IP nexthop replace id 102 group 100/101"

	# if the bug described in commit "net: nexthop: release IPv6 per-cpu
	# dsts when replacing a nexthop group" exists at this point we have
	# an unlinked IPv6 route (but not freed due to stale dst) with a
	# reference over the group so we delete the group which will again
	# only unlink it due to the route reference
	run_cmd "$IP nexthop del id 102"

	# delete the nexthop with stale dst, since we have an unlinked
	# group with a ref to it and an unlinked IPv6 route with ref to the
	# group, the nh will only be unlinked and not freed so the stale dst
	# remains forever and we get a net device refcount imbalance
	run_cmd "$IP nexthop del id 100"

	# if a reference was lost this command will hang because the net device
	# cannot be removed
	timeout -s KILL 5 ip netns exec $me ip link del veth1.10 >/dev/null 2>&1

	# we can't cleanup if the command is hung trying to delete the netdev
	if [ $? -eq 137 ]; then
		return 1
	fi

	# cleanup
	run_cmd "$IP link del veth1.20"
	run_cmd "$IP nexthop flush"

	return 0
}

ipv6_grp_fcnal()
{
	local rc

	echo
	echo "IPv6 groups functional"
	echo "----------------------"

	# basic functionality: create a nexthop group, default weight
	run_cmd "$IP nexthop add id 61 via 2001:db8:91::2 dev veth1"
	run_cmd "$IP nexthop add id 101 group 61"
	log_test $? 0 "Create nexthop group with single nexthop"

	# get nexthop group
	run_cmd "$IP nexthop get id 101"
	log_test $? 0 "Get nexthop group by id"
	check_nexthop "id 101" "id 101 group 61"

	# delete nexthop group
	run_cmd "$IP nexthop del id 101"
	log_test $? 0 "Delete nexthop group by id"
	check_nexthop "id 101" ""

	$IP nexthop flush >/dev/null 2>&1
	check_nexthop "id 101" ""

	#
	# create group with multiple nexthops - mix of gw and dev only
	#
	run_cmd "$IP nexthop add id 62 via 2001:db8:91::2 dev veth1"
	run_cmd "$IP nexthop add id 63 via 2001:db8:91::3 dev veth1"
	run_cmd "$IP nexthop add id 64 via 2001:db8:91::4 dev veth1"
	run_cmd "$IP nexthop add id 65 dev veth1"
	run_cmd "$IP nexthop add id 102 group 62/63/64/65"
	log_test $? 0 "Nexthop group with multiple nexthops"
	check_nexthop "id 102" "id 102 group 62/63/64/65"

	# Delete nexthop in a group and group is updated
	run_cmd "$IP nexthop del id 63"
	check_nexthop "id 102" "id 102 group 62/64/65"
	log_test $? 0 "Nexthop group updated when entry is deleted"

	# create group with multiple weighted nexthops
	run_cmd "$IP nexthop add id 63 via 2001:db8:91::3 dev veth1"
	run_cmd "$IP nexthop add id 103 group 62/63,2/64,3/65,4"
	log_test $? 0 "Nexthop group with weighted nexthops"
	check_nexthop "id 103" "id 103 group 62/63,2/64,3/65,4"

	# Delete nexthop in a weighted group and group is updated
	run_cmd "$IP nexthop del id 63"
	check_nexthop "id 103" "id 103 group 62/64,3/65,4"
	log_test $? 0 "Weighted nexthop group updated when entry is deleted"

	# admin down - nexthop is removed from group
	run_cmd "$IP li set dev veth1 down"
	check_nexthop "dev veth1" ""
	log_test $? 0 "Nexthops in groups removed on admin down"

	# expect groups to have been deleted as well
	check_nexthop "" ""

	run_cmd "$IP li set dev veth1 up"

	$IP nexthop flush >/dev/null 2>&1

	# group with nexthops using different devices
	set -e
	run_cmd "$IP nexthop add id 62 via 2001:db8:91::2 dev veth1"
	run_cmd "$IP nexthop add id 63 via 2001:db8:91::3 dev veth1"
	run_cmd "$IP nexthop add id 64 via 2001:db8:91::4 dev veth1"
	run_cmd "$IP nexthop add id 65 via 2001:db8:91::5 dev veth1"

	run_cmd "$IP nexthop add id 72 via 2001:db8:92::2 dev veth3"
	run_cmd "$IP nexthop add id 73 via 2001:db8:92::3 dev veth3"
	run_cmd "$IP nexthop add id 74 via 2001:db8:92::4 dev veth3"
	run_cmd "$IP nexthop add id 75 via 2001:db8:92::5 dev veth3"
	set +e

	# multiple groups with same nexthop
	run_cmd "$IP nexthop add id 104 group 62"
	run_cmd "$IP nexthop add id 105 group 62"
	check_nexthop "group" "id 104 group 62 id 105 group 62"
	log_test $? 0 "Multiple groups with same nexthop"

	run_cmd "$IP nexthop flush groups"
	[ $? -ne 0 ] && return 1

	# on admin down of veth1, it should be removed from the group
	run_cmd "$IP nexthop add id 105 group 62/63/72/73/64"
	run_cmd "$IP li set veth1 down"
	check_nexthop "id 105" "id 105 group 72/73"
	log_test $? 0 "Nexthops in group removed on admin down - mixed group"

	run_cmd "$IP nexthop add id 106 group 105/74"
	log_test $? 2 "Nexthop group can not have a group as an entry"

	# a group can have a blackhole entry only if it is the only
	# nexthop in the group. Needed for atomic replace with an
	# actual nexthop group
	run_cmd "$IP -6 nexthop add id 31 blackhole"
	run_cmd "$IP nexthop add id 107 group 31"
	log_test $? 0 "Nexthop group with a blackhole entry"

	run_cmd "$IP nexthop add id 108 group 31/24"
	log_test $? 2 "Nexthop group can not have a blackhole and another nexthop"

	ipv6_grp_refs
	log_test $? 0 "Nexthop group replace refcounts"
}

ipv6_res_grp_fcnal()
{
	local rc

	echo
	echo "IPv6 resilient groups functional"
	echo "--------------------------------"

	check_nexthop_res_support
	if [ $? -eq $ksft_skip ]; then
		return $ksft_skip
	fi

	#
	# migration of nexthop buckets - equal weights
	#
	run_cmd "$IP nexthop add id 62 via 2001:db8:91::2 dev veth1"
	run_cmd "$IP nexthop add id 63 via 2001:db8:91::3 dev veth1"
	run_cmd "$IP nexthop add id 102 group 62/63 type resilient buckets 2 idle_timer 0"

	run_cmd "$IP nexthop del id 63"
	check_nexthop "id 102" \
		"id 102 group 62 type resilient buckets 2 idle_timer 0 unbalanced_timer 0 unbalanced_time 0"
	log_test $? 0 "Nexthop group updated when entry is deleted"
	check_nexthop_bucket "list id 102" \
		"id 102 index 0 nhid 62 id 102 index 1 nhid 62"
	log_test $? 0 "Nexthop buckets updated when entry is deleted"

	run_cmd "$IP nexthop add id 63 via 2001:db8:91::3 dev veth1"
	run_cmd "$IP nexthop replace id 102 group 62/63 type resilient buckets 2 idle_timer 0"
	check_nexthop "id 102" \
		"id 102 group 62/63 type resilient buckets 2 idle_timer 0 unbalanced_timer 0 unbalanced_time 0"
	log_test $? 0 "Nexthop group updated after replace"
	check_nexthop_bucket "list id 102" \
		"id 102 index 0 nhid 63 id 102 index 1 nhid 62"
	log_test $? 0 "Nexthop buckets updated after replace"

	$IP nexthop flush >/dev/null 2>&1

	#
	# migration of nexthop buckets - unequal weights
	#
	run_cmd "$IP nexthop add id 62 via 2001:db8:91::2 dev veth1"
	run_cmd "$IP nexthop add id 63 via 2001:db8:91::3 dev veth1"
	run_cmd "$IP nexthop add id 102 group 62,3/63,1 type resilient buckets 4 idle_timer 0"

	run_cmd "$IP nexthop del id 63"
	check_nexthop "id 102" \
		"id 102 group 62,3 type resilient buckets 4 idle_timer 0 unbalanced_timer 0 unbalanced_time 0"
	log_test $? 0 "Nexthop group updated when entry is deleted - nECMP"
	check_nexthop_bucket "list id 102" \
		"id 102 index 0 nhid 62 id 102 index 1 nhid 62 id 102 index 2 nhid 62 id 102 index 3 nhid 62"
	log_test $? 0 "Nexthop buckets updated when entry is deleted - nECMP"

	run_cmd "$IP nexthop add id 63 via 2001:db8:91::3 dev veth1"
	run_cmd "$IP nexthop replace id 102 group 62,3/63,1 type resilient buckets 4 idle_timer 0"
	check_nexthop "id 102" \
		"id 102 group 62,3/63 type resilient buckets 4 idle_timer 0 unbalanced_timer 0 unbalanced_time 0"
	log_test $? 0 "Nexthop group updated after replace - nECMP"
	check_nexthop_bucket "list id 102" \
		"id 102 index 0 nhid 63 id 102 index 1 nhid 62 id 102 index 2 nhid 62 id 102 index 3 nhid 62"
	log_test $? 0 "Nexthop buckets updated after replace - nECMP"
}

ipv6_fcnal_runtime()
{
	local rc

	echo
	echo "IPv6 functional runtime"
	echo "-----------------------"

	#
	# IPv6 - the basics
	#
	run_cmd "$IP nexthop add id 81 via 2001:db8:91::2 dev veth1"
	run_cmd "$IP ro add 2001:db8:101::1/128 nhid 81"
	log_test $? 0 "Route add"

	run_cmd "$IP ro delete 2001:db8:101::1/128 nhid 81"
	log_test $? 0 "Route delete"

	run_cmd "$IP ro add 2001:db8:101::1/128 nhid 81"
	run_cmd "ip netns exec $me ping -c1 -w$PING_TIMEOUT 2001:db8:101::1"
	log_test $? 0 "Ping with nexthop"

	run_cmd "$IP nexthop add id 82 via 2001:db8:92::2 dev veth3"
	run_cmd "$IP nexthop add id 122 group 81/82"
	run_cmd "$IP ro replace 2001:db8:101::1/128 nhid 122"
	run_cmd "ip netns exec $me ping -c1 -w$PING_TIMEOUT 2001:db8:101::1"
	log_test $? 0 "Ping - multipath"

	#
	# IPv6 with blackhole nexthops
	#
	run_cmd "$IP -6 nexthop add id 83 blackhole"
	run_cmd "$IP ro replace 2001:db8:101::1/128 nhid 83"
	run_cmd "ip netns exec $me ping -c1 -w$PING_TIMEOUT 2001:db8:101::1"
	log_test $? 2 "Ping - blackhole"

	run_cmd "$IP nexthop replace id 83 via 2001:db8:91::2 dev veth1"
	run_cmd "ip netns exec $me ping -c1 -w$PING_TIMEOUT 2001:db8:101::1"
	log_test $? 0 "Ping - blackhole replaced with gateway"

	run_cmd "$IP -6 nexthop replace id 83 blackhole"
	run_cmd "ip netns exec $me ping -c1 -w$PING_TIMEOUT 2001:db8:101::1"
	log_test $? 2 "Ping - gateway replaced by blackhole"

	run_cmd "$IP ro replace 2001:db8:101::1/128 nhid 122"
	run_cmd "ip netns exec $me ping -c1 -w$PING_TIMEOUT 2001:db8:101::1"
	if [ $? -eq 0 ]; then
		run_cmd "$IP nexthop replace id 122 group 83"
		run_cmd "ip netns exec $me ping -c1 -w$PING_TIMEOUT 2001:db8:101::1"
		log_test $? 2 "Ping - group with blackhole"

		run_cmd "$IP nexthop replace id 122 group 81/82"
		run_cmd "ip netns exec $me ping -c1 -w$PING_TIMEOUT 2001:db8:101::1"
		log_test $? 0 "Ping - group blackhole replaced with gateways"
	else
		log_test 2 0 "Ping - multipath failed"
	fi

	#
	# device only and gw + dev only mix
	#
	run_cmd "$IP -6 nexthop add id 85 dev veth1"
	run_cmd "$IP ro replace 2001:db8:101::1/128 nhid 85"
	log_test $? 0 "IPv6 route with device only nexthop"
	check_route6 "2001:db8:101::1" "2001:db8:101::1 nhid 85 dev veth1 metric 1024"

	run_cmd "$IP nexthop add id 123 group 81/85"
	run_cmd "$IP ro replace 2001:db8:101::1/128 nhid 123"
	log_test $? 0 "IPv6 multipath route with nexthop mix - dev only + gw"
	check_route6 "2001:db8:101::1" "2001:db8:101::1 nhid 123 metric 1024 nexthop via 2001:db8:91::2 dev veth1 weight 1 nexthop dev veth1 weight 1"

	#
	# IPv6 route with v4 nexthop - not allowed
	#
	run_cmd "$IP ro delete 2001:db8:101::1/128"
	run_cmd "$IP nexthop add id 84 via 172.16.1.1 dev veth1"
	run_cmd "$IP ro add 2001:db8:101::1/128 nhid 84"
	log_test $? 2 "IPv6 route can not have a v4 gateway"

	run_cmd "$IP ro replace 2001:db8:101::1/128 nhid 81"
	run_cmd "$IP nexthop replace id 81 via 172.16.1.1 dev veth1"
	log_test $? 2 "Nexthop replace - v6 route, v4 nexthop"

	run_cmd "$IP ro replace 2001:db8:101::1/128 nhid 122"
	run_cmd "$IP nexthop replace id 81 via 172.16.1.1 dev veth1"
	log_test $? 2 "Nexthop replace of group entry - v6 route, v4 nexthop"

	run_cmd "$IP nexthop add id 86 via 2001:db8:92::2 dev veth3"
	run_cmd "$IP nexthop add id 87 via 172.16.1.1 dev veth1"
	run_cmd "$IP nexthop add id 88 via 172.16.1.1 dev veth1"
	run_cmd "$IP nexthop add id 124 group 86/87/88"
	run_cmd "$IP ro replace 2001:db8:101::1/128 nhid 124"
	log_test $? 2 "IPv6 route can not have a group with v4 and v6 gateways"

	run_cmd "$IP nexthop del id 88"
	run_cmd "$IP ro replace 2001:db8:101::1/128 nhid 124"
	log_test $? 2 "IPv6 route can not have a group with v4 and v6 gateways"

	run_cmd "$IP nexthop del id 87"
	run_cmd "$IP ro replace 2001:db8:101::1/128 nhid 124"
	log_test $? 0 "IPv6 route using a group after removing v4 gateways"

	run_cmd "$IP ro delete 2001:db8:101::1/128"
	run_cmd "$IP nexthop add id 87 via 172.16.1.1 dev veth1"
	run_cmd "$IP nexthop add id 88 via 172.16.1.1 dev veth1"
	run_cmd "$IP nexthop replace id 124 group 86/87/88"
	run_cmd "$IP ro replace 2001:db8:101::1/128 nhid 124"
	log_test $? 2 "IPv6 route can not have a group with v4 and v6 gateways"

	run_cmd "$IP nexthop replace id 88 via 2001:db8:92::2 dev veth3"
	run_cmd "$IP ro replace 2001:db8:101::1/128 nhid 124"
	log_test $? 2 "IPv6 route can not have a group with v4 and v6 gateways"

	run_cmd "$IP nexthop replace id 87 via 2001:db8:92::2 dev veth3"
	run_cmd "$IP ro replace 2001:db8:101::1/128 nhid 124"
	log_test $? 0 "IPv6 route using a group after replacing v4 gateways"

	$IP nexthop flush >/dev/null 2>&1

	#
	# weird IPv6 cases
	#
	run_cmd "$IP nexthop add id 86 via 2001:db8:91::2 dev veth1"
	run_cmd "$IP ro add 2001:db8:101::1/128 nhid 81"

	# route can not use prefsrc with nexthops
	run_cmd "$IP ro add 2001:db8:101::2/128 nhid 86 from 2001:db8:91::1"
	log_test $? 2 "IPv6 route can not use src routing with external nexthop"

	# check cleanup path on invalid metric
	run_cmd "$IP ro add 2001:db8:101::2/128 nhid 86 congctl lock foo"
	log_test $? 2 "IPv6 route with invalid metric"

	# rpfilter and default route
	$IP nexthop flush >/dev/null 2>&1
	run_cmd "ip netns exec $me ip6tables -t mangle -I PREROUTING 1 -m rpfilter --invert -j DROP"
	run_cmd "$IP nexthop add id 91 via 2001:db8:91::2 dev veth1"
	run_cmd "$IP nexthop add id 92 via 2001:db8:92::2 dev veth3"
	run_cmd "$IP nexthop add id 93 group 91/92"
	run_cmd "$IP -6 ro add default nhid 91"
	run_cmd "ip netns exec $me ping -c1 -w$PING_TIMEOUT 2001:db8:101::1"
	log_test $? 0 "Nexthop with default route and rpfilter"
	run_cmd "$IP -6 ro replace default nhid 93"
	run_cmd "ip netns exec $me ping -c1 -w$PING_TIMEOUT 2001:db8:101::1"
	log_test $? 0 "Nexthop with multipath default route and rpfilter"

	# TO-DO:
	# existing route with old nexthop; append route with new nexthop
	# existing route with old nexthop; replace route with new
	# existing route with new nexthop; replace route with old
	# route with src address and using nexthop - not allowed
}

ipv6_large_grp()
{
	local ecmp=32

	echo
	echo "IPv6 large groups (x$ecmp)"
	echo "---------------------"

	check_large_grp 6 $ecmp

	$IP nexthop flush >/dev/null 2>&1
}

ipv6_large_res_grp()
{
	echo
	echo "IPv6 large resilient group (128k buckets)"
	echo "-----------------------------------------"

	check_nexthop_res_support
	if [ $? -eq $ksft_skip ]; then
		return $ksft_skip
	fi

	check_large_res_grp 6 $((128 * 1024))

	$IP nexthop flush >/dev/null 2>&1
}

ipv6_del_add_loop1()
{
	while :; do
		$IP nexthop del id 100
		$IP nexthop add id 100 via 2001:db8:91::2 dev veth1
	done >/dev/null 2>&1
}

ipv6_grp_replace_loop()
{
	while :; do
		$IP nexthop replace id 102 group 100/101
	done >/dev/null 2>&1
}

ipv6_torture()
{
	local pid1
	local pid2
	local pid3
	local pid4
	local pid5

	echo
	echo "IPv6 runtime torture"
	echo "--------------------"
	if [ ! -x "$(command -v mausezahn)" ]; then
		echo "SKIP: Could not run test; need mausezahn tool"
		return
	fi

	run_cmd "$IP nexthop add id 100 via 2001:db8:91::2 dev veth1"
	run_cmd "$IP nexthop add id 101 via 2001:db8:92::2 dev veth3"
	run_cmd "$IP nexthop add id 102 group 100/101"
	run_cmd "$IP route add 2001:db8:101::1 nhid 102"
	run_cmd "$IP route add 2001:db8:101::2 nhid 102"

	ipv6_del_add_loop1 &
	pid1=$!
	ipv6_grp_replace_loop &
	pid2=$!
	ip netns exec $me ping -f 2001:db8:101::1 >/dev/null 2>&1 &
	pid3=$!
	ip netns exec $me ping -f 2001:db8:101::2 >/dev/null 2>&1 &
	pid4=$!
	ip netns exec $me mausezahn -6 veth1 -B 2001:db8:101::2 -A 2001:db8:91::1 -c 0 -t tcp "dp=1-1023, flags=syn" >/dev/null 2>&1 &
	pid5=$!

	sleep 300
	kill -9 $pid1 $pid2 $pid3 $pid4 $pid5
	wait $pid1 $pid2 $pid3 $pid4 $pid5 2>/dev/null

	# if we did not crash, success
	log_test 0 0 "IPv6 torture test"
}

ipv6_res_grp_replace_loop()
{
	while :; do
		$IP nexthop replace id 102 group 100/101 type resilient
	done >/dev/null 2>&1
}

ipv6_res_torture()
{
	local pid1
	local pid2
	local pid3
	local pid4
	local pid5

	echo
	echo "IPv6 runtime resilient nexthop group torture"
	echo "--------------------------------------------"

	check_nexthop_res_support
	if [ $? -eq $ksft_skip ]; then
		return $ksft_skip
	fi

	if [ ! -x "$(command -v mausezahn)" ]; then
		echo "SKIP: Could not run test; need mausezahn tool"
		return
	fi

	run_cmd "$IP nexthop add id 100 via 2001:db8:91::2 dev veth1"
	run_cmd "$IP nexthop add id 101 via 2001:db8:92::2 dev veth3"
	run_cmd "$IP nexthop add id 102 group 100/101 type resilient buckets 512 idle_timer 0"
	run_cmd "$IP route add 2001:db8:101::1 nhid 102"
	run_cmd "$IP route add 2001:db8:101::2 nhid 102"

	ipv6_del_add_loop1 &
	pid1=$!
	ipv6_res_grp_replace_loop &
	pid2=$!
	ip netns exec $me ping -f 2001:db8:101::1 >/dev/null 2>&1 &
	pid3=$!
	ip netns exec $me ping -f 2001:db8:101::2 >/dev/null 2>&1 &
	pid4=$!
	ip netns exec $me mausezahn -6 veth1 \
			    -B 2001:db8:101::2 -A 2001:db8:91::1 -c 0 \
			    -t tcp "dp=1-1023, flags=syn" >/dev/null 2>&1 &
	pid5=$!

	sleep 300
	kill -9 $pid1 $pid2 $pid3 $pid4 $pid5
	wait $pid1 $pid2 $pid3 $pid4 $pid5 2>/dev/null

	# if we did not crash, success
	log_test 0 0 "IPv6 resilient nexthop group torture test"
}

ipv4_fcnal()
{
	local rc

	echo
	echo "IPv4 functional"
	echo "----------------------"

	#
	# basic IPv4 ops - add, get, delete
	#
	run_cmd "$IP nexthop add id 12 via 172.16.1.2 dev veth1"
	rc=$?
	log_test $rc 0 "Create nexthop with id, gw, dev"
	if [ $rc -ne 0 ]; then
		echo "Basic IPv4 create fails; can not continue"
		return 1
	fi

	run_cmd "$IP nexthop get id 12"
	log_test $? 0 "Get nexthop by id"
	check_nexthop "id 12" "id 12 via 172.16.1.2 dev veth1 scope link"

	run_cmd "$IP nexthop del id 12"
	log_test $? 0 "Delete nexthop by id"
	check_nexthop "id 52" ""

	#
	# gw, device spec
	#
	# gw validation, no device - fails since dev is required
	run_cmd "$IP nexthop add id 12 via 172.16.2.3"
	log_test $? 2 "Create nexthop - gw only"

	# gw not reachable through given dev
	run_cmd "$IP nexthop add id 13 via 172.16.3.2 dev veth1"
	log_test $? 2 "Create nexthop - invalid gw+dev combination"

	# onlink flag overrides gw+dev lookup
	run_cmd "$IP nexthop add id 13 via 172.16.3.2 dev veth1 onlink"
	log_test $? 0 "Create nexthop - gw+dev and onlink"

	# admin down should delete nexthops
	set -e
	run_cmd "$IP nexthop add id 15 via 172.16.1.3 dev veth1"
	run_cmd "$IP nexthop add id 16 via 172.16.1.4 dev veth1"
	run_cmd "$IP nexthop add id 17 via 172.16.1.5 dev veth1"
	run_cmd "$IP li set dev veth1 down"
	set +e
	check_nexthop "dev veth1" ""
	log_test $? 0 "Nexthops removed on admin down"

	# nexthop route delete warning: route add with nhid and delete
	# using device
	run_cmd "$IP li set dev veth1 up"
	run_cmd "$IP nexthop add id 12 via 172.16.1.3 dev veth1"
	out1=`dmesg | grep "WARNING:.*fib_nh_match.*" | wc -l`
	run_cmd "$IP route add 172.16.101.1/32 nhid 12"
	run_cmd "$IP route delete 172.16.101.1/32 dev veth1"
	out2=`dmesg | grep "WARNING:.*fib_nh_match.*" | wc -l`
	[ $out1 -eq $out2 ]
	rc=$?
	log_test $rc 0 "Delete nexthop route warning"
	run_cmd "$IP route delete 172.16.101.1/32 nhid 12"
	run_cmd "$IP nexthop del id 12"

	run_cmd "$IP nexthop add id 21 via 172.16.1.6 dev veth1"
	run_cmd "$IP ro add 172.16.101.0/24 nhid 21"
	run_cmd "$IP ro del 172.16.101.0/24 nexthop via 172.16.1.7 dev veth1 nexthop via 172.16.1.8 dev veth1"
	log_test $? 2 "Delete multipath route with only nh id based entry"

	run_cmd "$IP nexthop add id 22 via 172.16.1.6 dev veth1"
	run_cmd "$IP ro add 172.16.102.0/24 nhid 22"
	run_cmd "$IP ro del 172.16.102.0/24 dev veth1"
	log_test $? 2 "Delete route when specifying only nexthop device"

	run_cmd "$IP ro del 172.16.102.0/24 via 172.16.1.6"
	log_test $? 2 "Delete route when specifying only gateway"

	run_cmd "$IP ro del 172.16.102.0/24"
	log_test $? 0 "Delete route when not specifying nexthop attributes"
}

ipv4_grp_fcnal()
{
	local rc

	echo
	echo "IPv4 groups functional"
	echo "----------------------"

	# basic functionality: create a nexthop group, default weight
	run_cmd "$IP nexthop add id 11 via 172.16.1.2 dev veth1"
	run_cmd "$IP nexthop add id 101 group 11"
	log_test $? 0 "Create nexthop group with single nexthop"

	# get nexthop group
	run_cmd "$IP nexthop get id 101"
	log_test $? 0 "Get nexthop group by id"
	check_nexthop "id 101" "id 101 group 11"

	# delete nexthop group
	run_cmd "$IP nexthop del id 101"
	log_test $? 0 "Delete nexthop group by id"
	check_nexthop "id 101" ""

	$IP nexthop flush >/dev/null 2>&1

	#
	# create group with multiple nexthops
	run_cmd "$IP nexthop add id 12 via 172.16.1.2 dev veth1"
	run_cmd "$IP nexthop add id 13 via 172.16.1.3 dev veth1"
	run_cmd "$IP nexthop add id 14 via 172.16.1.4 dev veth1"
	run_cmd "$IP nexthop add id 15 via 172.16.1.5 dev veth1"
	run_cmd "$IP nexthop add id 102 group 12/13/14/15"
	log_test $? 0 "Nexthop group with multiple nexthops"
	check_nexthop "id 102" "id 102 group 12/13/14/15"

	# Delete nexthop in a group and group is updated
	run_cmd "$IP nexthop del id 13"
	check_nexthop "id 102" "id 102 group 12/14/15"
	log_test $? 0 "Nexthop group updated when entry is deleted"

	# create group with multiple weighted nexthops
	run_cmd "$IP nexthop add id 13 via 172.16.1.3 dev veth1"
	run_cmd "$IP nexthop add id 103 group 12/13,2/14,3/15,4"
	log_test $? 0 "Nexthop group with weighted nexthops"
	check_nexthop "id 103" "id 103 group 12/13,2/14,3/15,4"

	# Delete nexthop in a weighted group and group is updated
	run_cmd "$IP nexthop del id 13"
	check_nexthop "id 103" "id 103 group 12/14,3/15,4"
	log_test $? 0 "Weighted nexthop group updated when entry is deleted"

	# admin down - nexthop is removed from group
	run_cmd "$IP li set dev veth1 down"
	check_nexthop "dev veth1" ""
	log_test $? 0 "Nexthops in groups removed on admin down"

	# expect groups to have been deleted as well
	check_nexthop "" ""

	run_cmd "$IP li set dev veth1 up"

	$IP nexthop flush >/dev/null 2>&1

	# group with nexthops using different devices
	set -e
	run_cmd "$IP nexthop add id 12 via 172.16.1.2 dev veth1"
	run_cmd "$IP nexthop add id 13 via 172.16.1.3 dev veth1"
	run_cmd "$IP nexthop add id 14 via 172.16.1.4 dev veth1"
	run_cmd "$IP nexthop add id 15 via 172.16.1.5 dev veth1"

	run_cmd "$IP nexthop add id 22 via 172.16.2.2 dev veth3"
	run_cmd "$IP nexthop add id 23 via 172.16.2.3 dev veth3"
	run_cmd "$IP nexthop add id 24 via 172.16.2.4 dev veth3"
	run_cmd "$IP nexthop add id 25 via 172.16.2.5 dev veth3"
	set +e

	# multiple groups with same nexthop
	run_cmd "$IP nexthop add id 104 group 12"
	run_cmd "$IP nexthop add id 105 group 12"
	check_nexthop "group" "id 104 group 12 id 105 group 12"
	log_test $? 0 "Multiple groups with same nexthop"

	run_cmd "$IP nexthop flush groups"
	[ $? -ne 0 ] && return 1

	# on admin down of veth1, it should be removed from the group
	run_cmd "$IP nexthop add id 105 group 12/13/22/23/14"
	run_cmd "$IP li set veth1 down"
	check_nexthop "id 105" "id 105 group 22/23"
	log_test $? 0 "Nexthops in group removed on admin down - mixed group"

	run_cmd "$IP nexthop add id 106 group 105/24"
	log_test $? 2 "Nexthop group can not have a group as an entry"

	# a group can have a blackhole entry only if it is the only
	# nexthop in the group. Needed for atomic replace with an
	# actual nexthop group
	run_cmd "$IP nexthop add id 31 blackhole"
	run_cmd "$IP nexthop add id 107 group 31"
	log_test $? 0 "Nexthop group with a blackhole entry"

	run_cmd "$IP nexthop add id 108 group 31/24"
	log_test $? 2 "Nexthop group can not have a blackhole and another nexthop"
}

ipv4_res_grp_fcnal()
{
	local rc

	echo
	echo "IPv4 resilient groups functional"
	echo "--------------------------------"

	check_nexthop_res_support
	if [ $? -eq $ksft_skip ]; then
		return $ksft_skip
	fi

	#
	# migration of nexthop buckets - equal weights
	#
	run_cmd "$IP nexthop add id 12 via 172.16.1.2 dev veth1"
	run_cmd "$IP nexthop add id 13 via 172.16.1.3 dev veth1"
	run_cmd "$IP nexthop add id 102 group 12/13 type resilient buckets 2 idle_timer 0"

	run_cmd "$IP nexthop del id 13"
	check_nexthop "id 102" \
		"id 102 group 12 type resilient buckets 2 idle_timer 0 unbalanced_timer 0 unbalanced_time 0"
	log_test $? 0 "Nexthop group updated when entry is deleted"
	check_nexthop_bucket "list id 102" \
		"id 102 index 0 nhid 12 id 102 index 1 nhid 12"
	log_test $? 0 "Nexthop buckets updated when entry is deleted"

	run_cmd "$IP nexthop add id 13 via 172.16.1.3 dev veth1"
	run_cmd "$IP nexthop replace id 102 group 12/13 type resilient buckets 2 idle_timer 0"
	check_nexthop "id 102" \
		"id 102 group 12/13 type resilient buckets 2 idle_timer 0 unbalanced_timer 0 unbalanced_time 0"
	log_test $? 0 "Nexthop group updated after replace"
	check_nexthop_bucket "list id 102" \
		"id 102 index 0 nhid 13 id 102 index 1 nhid 12"
	log_test $? 0 "Nexthop buckets updated after replace"

	$IP nexthop flush >/dev/null 2>&1

	#
	# migration of nexthop buckets - unequal weights
	#
	run_cmd "$IP nexthop add id 12 via 172.16.1.2 dev veth1"
	run_cmd "$IP nexthop add id 13 via 172.16.1.3 dev veth1"
	run_cmd "$IP nexthop add id 102 group 12,3/13,1 type resilient buckets 4 idle_timer 0"

	run_cmd "$IP nexthop del id 13"
	check_nexthop "id 102" \
		"id 102 group 12,3 type resilient buckets 4 idle_timer 0 unbalanced_timer 0 unbalanced_time 0"
	log_test $? 0 "Nexthop group updated when entry is deleted - nECMP"
	check_nexthop_bucket "list id 102" \
		"id 102 index 0 nhid 12 id 102 index 1 nhid 12 id 102 index 2 nhid 12 id 102 index 3 nhid 12"
	log_test $? 0 "Nexthop buckets updated when entry is deleted - nECMP"

	run_cmd "$IP nexthop add id 13 via 172.16.1.3 dev veth1"
	run_cmd "$IP nexthop replace id 102 group 12,3/13,1 type resilient buckets 4 idle_timer 0"
	check_nexthop "id 102" \
		"id 102 group 12,3/13 type resilient buckets 4 idle_timer 0 unbalanced_timer 0 unbalanced_time 0"
	log_test $? 0 "Nexthop group updated after replace - nECMP"
	check_nexthop_bucket "list id 102" \
		"id 102 index 0 nhid 13 id 102 index 1 nhid 12 id 102 index 2 nhid 12 id 102 index 3 nhid 12"
	log_test $? 0 "Nexthop buckets updated after replace - nECMP"
}

ipv4_withv6_fcnal()
{
	local lladdr

	set -e
	lladdr=$(get_linklocal veth2 $peer)
	run_cmd "$IP nexthop add id 11 via ${lladdr} dev veth1"
	set +e
	run_cmd "$IP ro add 172.16.101.1/32 nhid 11"
	log_test $? 0 "IPv6 nexthop with IPv4 route"
	check_route "172.16.101.1" "172.16.101.1 nhid 11 via inet6 ${lladdr} dev veth1"

	set -e
	run_cmd "$IP nexthop add id 12 via 172.16.1.2 dev veth1"
	run_cmd "$IP nexthop add id 101 group 11/12"
	set +e
	run_cmd "$IP ro replace 172.16.101.1/32 nhid 101"
	log_test $? 0 "IPv6 nexthop with IPv4 route"

	check_route "172.16.101.1" "172.16.101.1 nhid 101 nexthop via inet6 ${lladdr} dev veth1 weight 1 nexthop via 172.16.1.2 dev veth1 weight 1"

	run_cmd "$IP ro replace 172.16.101.1/32 via inet6 ${lladdr} dev veth1"
	log_test $? 0 "IPv4 route with IPv6 gateway"
	check_route "172.16.101.1" "172.16.101.1 via inet6 ${lladdr} dev veth1"

	run_cmd "$IP ro replace 172.16.101.1/32 via inet6 2001:db8:50::1 dev veth1"
	log_test $? 2 "IPv4 route with invalid IPv6 gateway"
}

ipv4_fcnal_runtime()
{
	local lladdr
	local rc

	echo
	echo "IPv4 functional runtime"
	echo "-----------------------"

	run_cmd "$IP nexthop add id 21 via 172.16.1.2 dev veth1"
	run_cmd "$IP ro add 172.16.101.1/32 nhid 21"
	log_test $? 0 "Route add"
	check_route "172.16.101.1" "172.16.101.1 nhid 21 via 172.16.1.2 dev veth1"

	run_cmd "$IP ro delete 172.16.101.1/32 nhid 21"
	log_test $? 0 "Route delete"

	#
	# scope mismatch
	#
	run_cmd "$IP nexthop add id 22 via 172.16.1.2 dev veth1"
	run_cmd "$IP ro add 172.16.101.1/32 nhid 22 scope host"
	log_test $? 2 "Route add - scope conflict with nexthop"

	run_cmd "$IP nexthop replace id 22 dev veth3"
	run_cmd "$IP ro add 172.16.101.1/32 nhid 22 scope host"
	run_cmd "$IP nexthop replace id 22 via 172.16.2.2 dev veth3"
	log_test $? 2 "Nexthop replace with invalid scope for existing route"

	# check cleanup path on invalid metric
	run_cmd "$IP ro add 172.16.101.2/32 nhid 22 congctl lock foo"
	log_test $? 2 "IPv4 route with invalid metric"

	#
	# add route with nexthop and check traffic
	#
	run_cmd "$IP nexthop replace id 21 via 172.16.1.2 dev veth1"
	run_cmd "$IP ro replace 172.16.101.1/32 nhid 21"
	run_cmd "ip netns exec $me ping -c1 -w$PING_TIMEOUT 172.16.101.1"
	log_test $? 0 "Basic ping"

	run_cmd "$IP nexthop replace id 22 via 172.16.2.2 dev veth3"
	run_cmd "$IP nexthop add id 122 group 21/22"
	run_cmd "$IP ro replace 172.16.101.1/32 nhid 122"
	run_cmd "ip netns exec $me ping -c1 -w$PING_TIMEOUT 172.16.101.1"
	log_test $? 0 "Ping - multipath"

	run_cmd "$IP ro delete 172.16.101.1/32 nhid 122"

	#
	# multiple default routes
	# - tests fib_select_default
	run_cmd "$IP nexthop add id 501 via 172.16.1.2 dev veth1"
	run_cmd "$IP ro add default nhid 501"
	run_cmd "$IP ro add default via 172.16.1.3 dev veth1 metric 20"
	run_cmd "ip netns exec $me ping -c1 -w$PING_TIMEOUT 172.16.101.1"
	log_test $? 0 "Ping - multiple default routes, nh first"

	# flip the order
	run_cmd "$IP ro del default nhid 501"
	run_cmd "$IP ro del default via 172.16.1.3 dev veth1 metric 20"
	run_cmd "$IP ro add default via 172.16.1.2 dev veth1 metric 20"
	run_cmd "$IP nexthop replace id 501 via 172.16.1.3 dev veth1"
	run_cmd "$IP ro add default nhid 501 metric 20"
	run_cmd "ip netns exec $me ping -c1 -w$PING_TIMEOUT 172.16.101.1"
	log_test $? 0 "Ping - multiple default routes, nh second"

	run_cmd "$IP nexthop delete nhid 501"
	run_cmd "$IP ro del default"

	#
	# IPv4 with blackhole nexthops
	#
	run_cmd "$IP nexthop add id 23 blackhole"
	run_cmd "$IP ro replace 172.16.101.1/32 nhid 23"
	run_cmd "ip netns exec $me ping -c1 -w$PING_TIMEOUT 172.16.101.1"
	log_test $? 2 "Ping - blackhole"

	run_cmd "$IP nexthop replace id 23 via 172.16.1.2 dev veth1"
	run_cmd "ip netns exec $me ping -c1 -w$PING_TIMEOUT 172.16.101.1"
	log_test $? 0 "Ping - blackhole replaced with gateway"

	run_cmd "$IP nexthop replace id 23 blackhole"
	run_cmd "ip netns exec $me ping -c1 -w$PING_TIMEOUT 172.16.101.1"
	log_test $? 2 "Ping - gateway replaced by blackhole"

	run_cmd "$IP ro replace 172.16.101.1/32 nhid 122"
	run_cmd "ip netns exec $me ping -c1 -w$PING_TIMEOUT 172.16.101.1"
	if [ $? -eq 0 ]; then
		run_cmd "$IP nexthop replace id 122 group 23"
		run_cmd "ip netns exec $me ping -c1 -w$PING_TIMEOUT 172.16.101.1"
		log_test $? 2 "Ping - group with blackhole"

		run_cmd "$IP nexthop replace id 122 group 21/22"
		run_cmd "ip netns exec $me ping -c1 -w$PING_TIMEOUT 172.16.101.1"
		log_test $? 0 "Ping - group blackhole replaced with gateways"
	else
		log_test 2 0 "Ping - multipath failed"
	fi

	#
	# device only and gw + dev only mix
	#
	run_cmd "$IP nexthop add id 85 dev veth1"
	run_cmd "$IP ro replace 172.16.101.1/32 nhid 85"
	log_test $? 0 "IPv4 route with device only nexthop"
	check_route "172.16.101.1" "172.16.101.1 nhid 85 dev veth1"

	run_cmd "$IP nexthop add id 123 group 21/85"
	run_cmd "$IP ro replace 172.16.101.1/32 nhid 123"
	log_test $? 0 "IPv4 multipath route with nexthop mix - dev only + gw"
	check_route "172.16.101.1" "172.16.101.1 nhid 123 nexthop via 172.16.1.2 dev veth1 weight 1 nexthop dev veth1 weight 1"

	#
	# IPv4 with IPv6
	#
	set -e
	lladdr=$(get_linklocal veth2 $peer)
	run_cmd "$IP nexthop add id 24 via ${lladdr} dev veth1"
	set +e
	run_cmd "$IP ro replace 172.16.101.1/32 nhid 24"
	run_cmd "ip netns exec $me ping -c1 -w$PING_TIMEOUT 172.16.101.1"
	log_test $? 0 "IPv6 nexthop with IPv4 route"

	$IP neigh sh | grep -q "${lladdr} dev veth1"
	if [ $? -eq 1 ]; then
		echo "    WARNING: Neigh entry missing for ${lladdr}"
		$IP neigh sh | grep 'dev veth1'
	fi

	$IP neigh sh | grep -q "172.16.101.1 dev eth1"
	if [ $? -eq 0 ]; then
		echo "    WARNING: Neigh entry exists for 172.16.101.1"
		$IP neigh sh | grep 'dev veth1'
	fi

	set -e
	run_cmd "$IP nexthop add id 25 via 172.16.1.2 dev veth1"
	run_cmd "$IP nexthop add id 101 group 24/25"
	set +e
	run_cmd "$IP ro replace 172.16.101.1/32 nhid 101"
	log_test $? 0 "IPv4 route with mixed v4-v6 multipath route"

	check_route "172.16.101.1" "172.16.101.1 nhid 101 nexthop via inet6 ${lladdr} dev veth1 weight 1 nexthop via 172.16.1.2 dev veth1 weight 1"

	run_cmd "ip netns exec $me ping -c1 -w$PING_TIMEOUT 172.16.101.1"
	log_test $? 0 "IPv6 nexthop with IPv4 route"

	run_cmd "$IP ro replace 172.16.101.1/32 via inet6 ${lladdr} dev veth1"
	run_cmd "ip netns exec $me ping -c1 -w$PING_TIMEOUT 172.16.101.1"
	log_test $? 0 "IPv4 route with IPv6 gateway"

	$IP neigh sh | grep -q "${lladdr} dev veth1"
	if [ $? -eq 1 ]; then
		echo "    WARNING: Neigh entry missing for ${lladdr}"
		$IP neigh sh | grep 'dev veth1'
	fi

	$IP neigh sh | grep -q "172.16.101.1 dev eth1"
	if [ $? -eq 0 ]; then
		echo "    WARNING: Neigh entry exists for 172.16.101.1"
		$IP neigh sh | grep 'dev veth1'
	fi

	run_cmd "$IP ro del 172.16.101.1/32 via inet6 ${lladdr} dev veth1"
	run_cmd "$IP -4 ro add default via inet6 ${lladdr} dev veth1"
	run_cmd "ip netns exec $me ping -c1 -w$PING_TIMEOUT 172.16.101.1"
	log_test $? 0 "IPv4 default route with IPv6 gateway"

	#
	# MPLS as an example of LWT encap
	#
	run_cmd "$IP nexthop add id 51 encap mpls 101 via 172.16.1.2 dev veth1"
	log_test $? 0 "IPv4 route with MPLS encap"
	check_nexthop "id 51" "id 51 encap mpls 101 via 172.16.1.2 dev veth1 scope link"
	log_test $? 0 "IPv4 route with MPLS encap - check"

	run_cmd "$IP nexthop add id 52 encap mpls 102 via inet6 2001:db8:91::2 dev veth1"
	log_test $? 0 "IPv4 route with MPLS encap and v6 gateway"
	check_nexthop "id 52" "id 52 encap mpls 102 via 2001:db8:91::2 dev veth1 scope link"
	log_test $? 0 "IPv4 route with MPLS encap, v6 gw - check"
}

ipv4_large_grp()
{
	local ecmp=32

	echo
	echo "IPv4 large groups (x$ecmp)"
	echo "---------------------"

	check_large_grp 4 $ecmp

	$IP nexthop flush >/dev/null 2>&1
}

ipv4_large_res_grp()
{
	echo
	echo "IPv4 large resilient group (128k buckets)"
	echo "-----------------------------------------"

	check_nexthop_res_support
	if [ $? -eq $ksft_skip ]; then
		return $ksft_skip
	fi

	check_large_res_grp 4 $((128 * 1024))

	$IP nexthop flush >/dev/null 2>&1
}

sysctl_nexthop_compat_mode_check()
{
	local sysctlname="net.ipv4.nexthop_compat_mode"
	local lprefix=$1

	IPE="ip netns exec $me"

	$IPE sysctl -q $sysctlname 2>&1 >/dev/null
	if [ $? -ne 0 ]; then
		echo "SKIP: kernel lacks nexthop compat mode sysctl control"
		return $ksft_skip
	fi

	out=$($IPE sysctl $sysctlname 2>/dev/null)
	log_test $? 0 "$lprefix default nexthop compat mode check"
	check_output "${out}" "$sysctlname = 1"
}

sysctl_nexthop_compat_mode_set()
{
	local sysctlname="net.ipv4.nexthop_compat_mode"
	local mode=$1
	local lprefix=$2

	IPE="ip netns exec $me"

	out=$($IPE sysctl -w $sysctlname=$mode)
	log_test $? 0 "$lprefix set compat mode - $mode"
	check_output "${out}" "net.ipv4.nexthop_compat_mode = $mode"
}

ipv6_compat_mode()
{
	local rc

	echo
	echo "IPv6 nexthop api compat mode test"
	echo "--------------------------------"

	sysctl_nexthop_compat_mode_check "IPv6"
	if [ $? -eq $ksft_skip ]; then
		return $ksft_skip
	fi

	run_cmd "$IP nexthop add id 62 via 2001:db8:91::2 dev veth1"
	run_cmd "$IP nexthop add id 63 via 2001:db8:91::3 dev veth1"
	run_cmd "$IP nexthop add id 122 group 62/63"
	ipmout=$(start_ip_monitor route)

	run_cmd "$IP -6 ro add 2001:db8:101::1/128 nhid 122"
	# route add notification should contain expanded nexthops
	stop_ip_monitor $ipmout 3
	log_test $? 0 "IPv6 compat mode on - route add notification"

	# route dump should contain expanded nexthops
	check_route6 "2001:db8:101::1" "2001:db8:101::1 nhid 122 metric 1024 nexthop via 2001:db8:91::2 dev veth1 weight 1 nexthop via 2001:db8:91::3 dev veth1 weight 1"
	log_test $? 0 "IPv6 compat mode on - route dump"

	# change in nexthop group should generate route notification
	run_cmd "$IP nexthop add id 64 via 2001:db8:91::4 dev veth1"
	ipmout=$(start_ip_monitor route)
	run_cmd "$IP nexthop replace id 122 group 62/64"
	stop_ip_monitor $ipmout 3

	log_test $? 0 "IPv6 compat mode on - nexthop change"

	# set compat mode off
	sysctl_nexthop_compat_mode_set 0 "IPv6"

	run_cmd "$IP -6 ro del 2001:db8:101::1/128 nhid 122"

	run_cmd "$IP nexthop add id 62 via 2001:db8:91::2 dev veth1"
	run_cmd "$IP nexthop add id 63 via 2001:db8:91::3 dev veth1"
	run_cmd "$IP nexthop add id 122 group 62/63"
	ipmout=$(start_ip_monitor route)

	run_cmd "$IP -6 ro add 2001:db8:101::1/128 nhid 122"
	# route add notification should not contain expanded nexthops
	stop_ip_monitor $ipmout 1
	log_test $? 0 "IPv6 compat mode off - route add notification"

	# route dump should not contain expanded nexthops
	check_route6 "2001:db8:101::1" "2001:db8:101::1 nhid 122 metric 1024"
	log_test $? 0 "IPv6 compat mode off - route dump"

	# change in nexthop group should not generate route notification
	run_cmd "$IP nexthop add id 64 via 2001:db8:91::4 dev veth1"
	ipmout=$(start_ip_monitor route)
	run_cmd "$IP nexthop replace id 122 group 62/64"
	stop_ip_monitor $ipmout 0
	log_test $? 0 "IPv6 compat mode off - nexthop change"

	# nexthop delete should not generate route notification
	ipmout=$(start_ip_monitor route)
	run_cmd "$IP nexthop del id 122"
	stop_ip_monitor $ipmout 0
	log_test $? 0 "IPv6 compat mode off - nexthop delete"

	# set compat mode back on
	sysctl_nexthop_compat_mode_set 1 "IPv6"
}

ipv4_compat_mode()
{
	local rc

	echo
	echo "IPv4 nexthop api compat mode"
	echo "----------------------------"

	sysctl_nexthop_compat_mode_check "IPv4"
	if [ $? -eq $ksft_skip ]; then
		return $ksft_skip
	fi

	run_cmd "$IP nexthop add id 21 via 172.16.1.2 dev veth1"
	run_cmd "$IP nexthop add id 22 via 172.16.1.2 dev veth1"
	run_cmd "$IP nexthop add id 122 group 21/22"
	ipmout=$(start_ip_monitor route)

	run_cmd "$IP ro add 172.16.101.1/32 nhid 122"
	stop_ip_monitor $ipmout 3

	# route add notification should contain expanded nexthops
	log_test $? 0 "IPv4 compat mode on - route add notification"

	# route dump should contain expanded nexthops
	check_route "172.16.101.1" "172.16.101.1 nhid 122 nexthop via 172.16.1.2 dev veth1 weight 1 nexthop via 172.16.1.2 dev veth1 weight 1"
	log_test $? 0 "IPv4 compat mode on - route dump"

	# change in nexthop group should generate route notification
	run_cmd "$IP nexthop add id 23 via 172.16.1.3 dev veth1"
	ipmout=$(start_ip_monitor route)
	run_cmd "$IP nexthop replace id 122 group 21/23"
	stop_ip_monitor $ipmout 3
	log_test $? 0 "IPv4 compat mode on - nexthop change"

	sysctl_nexthop_compat_mode_set 0 "IPv4"

	# cleanup
	run_cmd "$IP ro del 172.16.101.1/32 nhid 122"

	ipmout=$(start_ip_monitor route)
	run_cmd "$IP ro add 172.16.101.1/32 nhid 122"
	stop_ip_monitor $ipmout 1
	# route add notification should not contain expanded nexthops
	log_test $? 0 "IPv4 compat mode off - route add notification"

	# route dump should not contain expanded nexthops
	check_route "172.16.101.1" "172.16.101.1 nhid 122"
	log_test $? 0 "IPv4 compat mode off - route dump"

	# change in nexthop group should not generate route notification
	ipmout=$(start_ip_monitor route)
	run_cmd "$IP nexthop replace id 122 group 21/22"
	stop_ip_monitor $ipmout 0
	log_test $? 0 "IPv4 compat mode off - nexthop change"

	# nexthop delete should not generate route notification
	ipmout=$(start_ip_monitor route)
	run_cmd "$IP nexthop del id 122"
	stop_ip_monitor $ipmout 0
	log_test $? 0 "IPv4 compat mode off - nexthop delete"

	sysctl_nexthop_compat_mode_set 1 "IPv4"
}

ipv4_del_add_loop1()
{
	while :; do
		$IP nexthop del id 100
		$IP nexthop add id 100 via 172.16.1.2 dev veth1
	done >/dev/null 2>&1
}

ipv4_grp_replace_loop()
{
	while :; do
		$IP nexthop replace id 102 group 100/101
	done >/dev/null 2>&1
}

ipv4_torture()
{
	local pid1
	local pid2
	local pid3
	local pid4
	local pid5

	echo
	echo "IPv4 runtime torture"
	echo "--------------------"
	if [ ! -x "$(command -v mausezahn)" ]; then
		echo "SKIP: Could not run test; need mausezahn tool"
		return
	fi

	run_cmd "$IP nexthop add id 100 via 172.16.1.2 dev veth1"
	run_cmd "$IP nexthop add id 101 via 172.16.2.2 dev veth3"
	run_cmd "$IP nexthop add id 102 group 100/101"
	run_cmd "$IP route add 172.16.101.1 nhid 102"
	run_cmd "$IP route add 172.16.101.2 nhid 102"

	ipv4_del_add_loop1 &
	pid1=$!
	ipv4_grp_replace_loop &
	pid2=$!
	ip netns exec $me ping -f 172.16.101.1 >/dev/null 2>&1 &
	pid3=$!
	ip netns exec $me ping -f 172.16.101.2 >/dev/null 2>&1 &
	pid4=$!
	ip netns exec $me mausezahn veth1 -B 172.16.101.2 -A 172.16.1.1 -c 0 -t tcp "dp=1-1023, flags=syn" >/dev/null 2>&1 &
	pid5=$!

	sleep 300
	kill -9 $pid1 $pid2 $pid3 $pid4 $pid5
	wait $pid1 $pid2 $pid3 $pid4 $pid5 2>/dev/null

	# if we did not crash, success
	log_test 0 0 "IPv4 torture test"
}

ipv4_res_grp_replace_loop()
{
	while :; do
		$IP nexthop replace id 102 group 100/101 type resilient
	done >/dev/null 2>&1
}

ipv4_res_torture()
{
	local pid1
	local pid2
	local pid3
	local pid4
	local pid5

	echo
	echo "IPv4 runtime resilient nexthop group torture"
	echo "--------------------------------------------"

	check_nexthop_res_support
	if [ $? -eq $ksft_skip ]; then
		return $ksft_skip
	fi

	if [ ! -x "$(command -v mausezahn)" ]; then
		echo "SKIP: Could not run test; need mausezahn tool"
		return
	fi

	run_cmd "$IP nexthop add id 100 via 172.16.1.2 dev veth1"
	run_cmd "$IP nexthop add id 101 via 172.16.2.2 dev veth3"
	run_cmd "$IP nexthop add id 102 group 100/101 type resilient buckets 512 idle_timer 0"
	run_cmd "$IP route add 172.16.101.1 nhid 102"
	run_cmd "$IP route add 172.16.101.2 nhid 102"

	ipv4_del_add_loop1 &
	pid1=$!
	ipv4_res_grp_replace_loop &
	pid2=$!
	ip netns exec $me ping -f 172.16.101.1 >/dev/null 2>&1 &
	pid3=$!
	ip netns exec $me ping -f 172.16.101.2 >/dev/null 2>&1 &
	pid4=$!
	ip netns exec $me mausezahn veth1 \
				-B 172.16.101.2 -A 172.16.1.1 -c 0 \
				-t tcp "dp=1-1023, flags=syn" >/dev/null 2>&1 &
	pid5=$!

	sleep 300
	kill -9 $pid1 $pid2 $pid3 $pid4 $pid5
	wait $pid1 $pid2 $pid3 $pid4 $pid5 2>/dev/null

	# if we did not crash, success
	log_test 0 0 "IPv4 resilient nexthop group torture test"
}

basic()
{
	echo
	echo "Basic functional tests"
	echo "----------------------"
	run_cmd "$IP nexthop ls"
	log_test $? 0 "List with nothing defined"

	run_cmd "$IP nexthop get id 1"
	log_test $? 2 "Nexthop get on non-existent id"

	# attempt to create nh without a device or gw - fails
	run_cmd "$IP nexthop add id 1"
	log_test $? 2 "Nexthop with no device or gateway"

	# attempt to create nh with down device - fails
	$IP li set veth1 down
	run_cmd "$IP nexthop add id 1 dev veth1"
	log_test $? 2 "Nexthop with down device"

	# create nh with linkdown device - fails
	$IP li set veth1 up
	ip -netns $peer li set veth2 down
	run_cmd "$IP nexthop add id 1 dev veth1"
	log_test $? 2 "Nexthop with device that is linkdown"
	ip -netns $peer li set veth2 up

	# device only
	run_cmd "$IP nexthop add id 1 dev veth1"
	log_test $? 0 "Nexthop with device only"

	# create nh with duplicate id
	run_cmd "$IP nexthop add id 1 dev veth3"
	log_test $? 2 "Nexthop with duplicate id"

	# blackhole nexthop
	run_cmd "$IP nexthop add id 2 blackhole"
	log_test $? 0 "Blackhole nexthop"

	# blackhole nexthop can not have other specs
	run_cmd "$IP nexthop replace id 2 blackhole dev veth1"
	log_test $? 2 "Blackhole nexthop with other attributes"

	# blackhole nexthop should not be affected by the state of the loopback
	# device
	run_cmd "$IP link set dev lo down"
	check_nexthop "id 2" "id 2 blackhole"
	log_test $? 0 "Blackhole nexthop with loopback device down"

	run_cmd "$IP link set dev lo up"

	# Dump should not loop endlessly when maximum nexthop ID is configured.
	run_cmd "$IP nexthop add id $((2**32-1)) blackhole"
	run_cmd "timeout 5 $IP nexthop"
	log_test $? 0 "Maximum nexthop ID dump"

	#
	# groups
	#

	run_cmd "$IP nexthop add id 101 group 1"
	log_test $? 0 "Create group"

	run_cmd "$IP nexthop add id 102 group 2"
	log_test $? 0 "Create group with blackhole nexthop"

	# multipath group can not have a blackhole as 1 path
	run_cmd "$IP nexthop add id 103 group 1/2"
	log_test $? 2 "Create multipath group where 1 path is a blackhole"

	# multipath group can not have a member replaced by a blackhole
	run_cmd "$IP nexthop replace id 2 dev veth3"
	run_cmd "$IP nexthop replace id 102 group 1/2"
	run_cmd "$IP nexthop replace id 2 blackhole"
	log_test $? 2 "Multipath group can not have a member replaced by blackhole"

	# attempt to create group with non-existent nexthop
	run_cmd "$IP nexthop add id 103 group 12"
	log_test $? 2 "Create group with non-existent nexthop"

	# attempt to create group with same nexthop
	run_cmd "$IP nexthop add id 103 group 1/1"
	log_test $? 2 "Create group with same nexthop multiple times"

	# replace nexthop with a group - fails
	run_cmd "$IP nexthop replace id 2 group 1"
	log_test $? 2 "Replace nexthop with nexthop group"

	# replace nexthop group with a nexthop - fails
	run_cmd "$IP nexthop replace id 101 dev veth1"
	log_test $? 2 "Replace nexthop group with nexthop"

	# nexthop group with other attributes fail
	run_cmd "$IP nexthop add id 104 group 1 dev veth1"
	log_test $? 2 "Nexthop group and device"

	# Tests to ensure that flushing works as expected.
	run_cmd "$IP nexthop add id 105 blackhole proto 99"
	run_cmd "$IP nexthop add id 106 blackhole proto 100"
	run_cmd "$IP nexthop add id 107 blackhole proto 99"
	run_cmd "$IP nexthop flush proto 99"
	check_nexthop "id 105" ""
	check_nexthop "id 106" "id 106 blackhole proto 100"
	check_nexthop "id 107" ""
	run_cmd "$IP nexthop flush proto 100"
	check_nexthop "id 106" ""

	run_cmd "$IP nexthop flush proto 100"
	log_test $? 0 "Test proto flush"

	run_cmd "$IP nexthop add id 104 group 1 blackhole"
	log_test $? 2 "Nexthop group and blackhole"

	$IP nexthop flush >/dev/null 2>&1

	# Test to ensure that flushing with a multi-part nexthop dump works as
	# expected.
	local batch_file=$(mktemp)

	for i in $(seq 1 $((64 * 1024))); do
		echo "nexthop add id $i blackhole" >> $batch_file
	done

	$IP -b $batch_file
	$IP nexthop flush >/dev/null 2>&1
	[[ $($IP nexthop | wc -l) -eq 0 ]]
	log_test $? 0 "Large scale nexthop flushing"

	rm $batch_file
}

check_nexthop_buckets_balance()
{
	local nharg=$1; shift
	local ret

	while (($# > 0)); do
		local selector=$1; shift
		local condition=$1; shift
		local count

		count=$($IP -j nexthop bucket ${nharg} ${selector} | jq length)
		(( $count $condition ))
		ret=$?
		if ((ret != 0)); then
			return $ret
		fi
	done

	return 0
}

basic_res()
{
	echo
	echo "Basic resilient nexthop group functional tests"
	echo "----------------------------------------------"

	check_nexthop_res_support
	if [ $? -eq $ksft_skip ]; then
		return $ksft_skip
	fi

	run_cmd "$IP nexthop add id 1 dev veth1"

	#
	# resilient nexthop group addition
	#

	run_cmd "$IP nexthop add id 101 group 1 type resilient buckets 8"
	log_test $? 0 "Add a nexthop group with default parameters"

	run_cmd "$IP nexthop get id 101"
	check_nexthop "id 101" \
		"id 101 group 1 type resilient buckets 8 idle_timer 120 unbalanced_timer 0 unbalanced_time 0"
	log_test $? 0 "Get a nexthop group with default parameters"

	run_cmd "$IP nexthop add id 102 group 1 type resilient
			buckets 4 idle_timer 100 unbalanced_timer 5"
	run_cmd "$IP nexthop get id 102"
	check_nexthop "id 102" \
		"id 102 group 1 type resilient buckets 4 idle_timer 100 unbalanced_timer 5 unbalanced_time 0"
	log_test $? 0 "Get a nexthop group with non-default parameters"

	run_cmd "$IP nexthop add id 103 group 1 type resilient buckets 0"
	log_test $? 2 "Add a nexthop group with 0 buckets"

	#
	# resilient nexthop group replacement
	#

	run_cmd "$IP nexthop replace id 101 group 1 type resilient
			buckets 8 idle_timer 240 unbalanced_timer 80"
	log_test $? 0 "Replace nexthop group parameters"
	check_nexthop "id 101" \
		"id 101 group 1 type resilient buckets 8 idle_timer 240 unbalanced_timer 80 unbalanced_time 0"
	log_test $? 0 "Get a nexthop group after replacing parameters"

	run_cmd "$IP nexthop replace id 101 group 1 type resilient idle_timer 512"
	log_test $? 0 "Replace idle timer"
	check_nexthop "id 101" \
		"id 101 group 1 type resilient buckets 8 idle_timer 512 unbalanced_timer 80 unbalanced_time 0"
	log_test $? 0 "Get a nexthop group after replacing idle timer"

	run_cmd "$IP nexthop replace id 101 group 1 type resilient unbalanced_timer 256"
	log_test $? 0 "Replace unbalanced timer"
	check_nexthop "id 101" \
		"id 101 group 1 type resilient buckets 8 idle_timer 512 unbalanced_timer 256 unbalanced_time 0"
	log_test $? 0 "Get a nexthop group after replacing unbalanced timer"

	run_cmd "$IP nexthop replace id 101 group 1 type resilient"
	log_test $? 0 "Replace with no parameters"
	check_nexthop "id 101" \
		"id 101 group 1 type resilient buckets 8 idle_timer 512 unbalanced_timer 256 unbalanced_time 0"
	log_test $? 0 "Get a nexthop group after replacing no parameters"

	run_cmd "$IP nexthop replace id 101 group 1"
	log_test $? 2 "Replace nexthop group type - implicit"

	run_cmd "$IP nexthop replace id 101 group 1 type mpath"
	log_test $? 2 "Replace nexthop group type - explicit"

	run_cmd "$IP nexthop replace id 101 group 1 type resilient buckets 1024"
	log_test $? 2 "Replace number of nexthop buckets"

	check_nexthop "id 101" \
		"id 101 group 1 type resilient buckets 8 idle_timer 512 unbalanced_timer 256 unbalanced_time 0"
	log_test $? 0 "Get a nexthop group after replacing with invalid parameters"

	#
	# resilient nexthop buckets dump
	#

	$IP nexthop flush >/dev/null 2>&1
	run_cmd "$IP nexthop add id 1 dev veth1"
	run_cmd "$IP nexthop add id 2 dev veth3"
	run_cmd "$IP nexthop add id 101 group 1/2 type resilient buckets 4"
	run_cmd "$IP nexthop add id 201 group 1/2"

	check_nexthop_bucket "" \
		"id 101 index 0 nhid 2 id 101 index 1 nhid 2 id 101 index 2 nhid 1 id 101 index 3 nhid 1"
	log_test $? 0 "Dump all nexthop buckets"

	check_nexthop_bucket "list id 101" \
		"id 101 index 0 nhid 2 id 101 index 1 nhid 2 id 101 index 2 nhid 1 id 101 index 3 nhid 1"
	log_test $? 0 "Dump all nexthop buckets in a group"

	sleep 0.1
	(( $($IP -j nexthop bucket list id 101 |
	     jq '[.[] | select(.bucket.idle_time > 0 and
	                       .bucket.idle_time < 2)] | length') == 4 ))
	log_test $? 0 "All nexthop buckets report a positive near-zero idle time"

	check_nexthop_bucket "list dev veth1" \
		"id 101 index 2 nhid 1 id 101 index 3 nhid 1"
	log_test $? 0 "Dump all nexthop buckets with a specific nexthop device"

	check_nexthop_bucket "list nhid 2" \
		"id 101 index 0 nhid 2 id 101 index 1 nhid 2"
	log_test $? 0 "Dump all nexthop buckets with a specific nexthop identifier"

	run_cmd "$IP nexthop bucket list id 111"
	log_test $? 2 "Dump all nexthop buckets in a non-existent group"

	run_cmd "$IP nexthop bucket list id 201"
	log_test $? 2 "Dump all nexthop buckets in a non-resilient group"

	run_cmd "$IP nexthop bucket list dev bla"
	log_test $? 255 "Dump all nexthop buckets using a non-existent device"

	run_cmd "$IP nexthop bucket list groups"
	log_test $? 255 "Dump all nexthop buckets with invalid 'groups' keyword"

	run_cmd "$IP nexthop bucket list fdb"
	log_test $? 255 "Dump all nexthop buckets with invalid 'fdb' keyword"

	# Dump should not loop endlessly when maximum nexthop ID is configured.
	run_cmd "$IP nexthop add id $((2**32-1)) group 1/2 type resilient buckets 4"
	run_cmd "timeout 5 $IP nexthop bucket"
	log_test $? 0 "Maximum nexthop ID dump"

	#
	# resilient nexthop buckets get requests
	#

	check_nexthop_bucket "get id 101 index 0" "id 101 index 0 nhid 2"
	log_test $? 0 "Get a valid nexthop bucket"

	run_cmd "$IP nexthop bucket get id 101 index 999"
	log_test $? 2 "Get a nexthop bucket with valid group, but invalid index"

	run_cmd "$IP nexthop bucket get id 201 index 0"
	log_test $? 2 "Get a nexthop bucket from a non-resilient group"

	run_cmd "$IP nexthop bucket get id 999 index 0"
	log_test $? 2 "Get a nexthop bucket from a non-existent group"

	#
	# tests for bucket migration
	#

	$IP nexthop flush >/dev/null 2>&1

	run_cmd "$IP nexthop add id 1 dev veth1"
	run_cmd "$IP nexthop add id 2 dev veth3"
	run_cmd "$IP nexthop add id 101
			group 1/2 type resilient buckets 10
			idle_timer 1 unbalanced_timer 20"

	check_nexthop_buckets_balance "list id 101" \
				      "nhid 1" "== 5" \
				      "nhid 2" "== 5"
	log_test $? 0 "Initial bucket allocation"

	run_cmd "$IP nexthop replace id 101
			group 1,2/2,3 type resilient"
	check_nexthop_buckets_balance "list id 101" \
				      "nhid 1" "== 4" \
				      "nhid 2" "== 6"
	log_test $? 0 "Bucket allocation after replace"

	# Check that increase in idle timer does not make buckets appear busy.
	run_cmd "$IP nexthop replace id 101
			group 1,2/2,3 type resilient
			idle_timer 10"
	run_cmd "$IP nexthop replace id 101
			group 1/2 type resilient"
	check_nexthop_buckets_balance "list id 101" \
				      "nhid 1" "== 5" \
				      "nhid 2" "== 5"
	log_test $? 0 "Buckets migrated after idle timer change"

	$IP nexthop flush >/dev/null 2>&1
}

################################################################################
# usage

usage()
{
	cat <<EOF
usage: ${0##*/} OPTS

        -t <test>   Test(s) to run (default: all)
                    (options: $ALL_TESTS)
        -4          IPv4 tests only
        -6          IPv6 tests only
        -p          Pause on fail
        -P          Pause after each test before cleanup
        -v          verbose mode (show commands and output)
	-w	    Timeout for ping

    Runtime test
	-n num	    Number of nexthops to target
	-N    	    Use new style to install routes in DUT

done
EOF
}

################################################################################
# main

while getopts :t:pP46hvw: o
do
	case $o in
		t) TESTS=$OPTARG;;
		4) TESTS=${IPV4_TESTS};;
		6) TESTS=${IPV6_TESTS};;
		p) PAUSE_ON_FAIL=yes;;
		P) PAUSE=yes;;
		v) VERBOSE=$(($VERBOSE + 1));;
		w) PING_TIMEOUT=$OPTARG;;
		h) usage; exit 0;;
		*) usage; exit 1;;
	esac
done

# make sure we don't pause twice
[ "${PAUSE}" = "yes" ] && PAUSE_ON_FAIL=no

if [ "$(id -u)" -ne 0 ];then
	echo "SKIP: Need root privileges"
	exit $ksft_skip;
fi

if [ ! -x "$(command -v ip)" ]; then
	echo "SKIP: Could not run test without ip tool"
	exit $ksft_skip
fi

ip help 2>&1 | grep -q nexthop
if [ $? -ne 0 ]; then
	echo "SKIP: iproute2 too old, missing nexthop command"
	exit $ksft_skip
fi

out=$(ip nexthop ls 2>&1 | grep -q "Operation not supported")
if [ $? -eq 0 ]; then
	echo "SKIP: kernel lacks nexthop support"
	exit $ksft_skip
fi

for t in $TESTS
do
	case $t in
	none) IP="ip -netns $peer"; setup; exit 0;;
	*) setup; $t; cleanup;;
	esac
done

if [ "$TESTS" != "none" ]; then
	printf "\nTests passed: %3d\n" ${nsuccess}
	printf "Tests failed: %3d\n"   ${nfail}
fi

exit $ret
