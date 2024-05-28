#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Validate cached routes in fib{6}_nh that is used by multiple prefixes.
# Validate a different # exception is generated in h0 for each remote host.
#
#               h1
#            /
#    h0 - r1 -  h2
#            \
#               h3
#
# routing in h0 to hN is done with nexthop objects.

source lib.sh
PAUSE_ON_FAIL=no
VERBOSE=0

which ping6 > /dev/null 2>&1 && ping6=$(which ping6) || ping6=$(which ping)

################################################################################
# helpers

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
		if [ "${PAUSE_ON_FAIL}" = "yes" ]; then
			echo
			echo "hit enter to continue, 'q' to quit"
			read a
			[ "$a" = "q" ] && exit 1
		fi
	fi

	[ "$VERBOSE" = "1" ] && echo
}

run_cmd()
{
	local cmd="$*"
	local out
	local rc

	if [ "$VERBOSE" = "1" ]; then
		echo "COMMAND: $cmd"
	fi

	out=$(eval $cmd 2>&1)
	rc=$?
	if [ "$VERBOSE" = "1" -a -n "$out" ]; then
		echo "$out"
	fi

	[ "$VERBOSE" = "1" ] && echo

	return $rc
}

################################################################################
# config

create_ns()
{
	local ns=${1}

	ip netns exec ${ns} sysctl -q -w net.ipv6.conf.all.keep_addr_on_down=1
	case ${ns} in
	h*)
		ip netns exec $ns sysctl -q -w net.ipv6.conf.all.forwarding=0
		;;
	r*)
		ip netns exec $ns sysctl -q -w net.ipv4.ip_forward=1
		ip netns exec $ns sysctl -q -w net.ipv6.conf.all.forwarding=1
		;;
	esac
}

setup()
{
	local ns
	local i

	#set -e

	setup_ns h0 r1 h1 h2 h3
	h[0]=$h0
	h[1]=$h1
	h[2]=$h2
	h[3]=$h3
	r[1]=$r1
	for ns in ${h[0]} ${r[1]} ${h[1]} ${h[2]} ${h[3]}
	do
		create_ns ${ns}
	done

	#
	# create interconnects
	#

	for i in 0 1 2 3
	do
		ip -netns ${h[$i]} li add eth0 type veth peer name r1h${i}
		ip -netns ${h[$i]} li set eth0 up
		ip -netns ${h[$i]} li set r1h${i} netns ${r[1]} name eth${i} up

		ip -netns ${h[$i]}    addr add dev eth0 172.16.10${i}.1/24
		ip -netns ${h[$i]} -6 addr add dev eth0 2001:db8:10${i}::1/64
		ip -netns ${r[1]}    addr add dev eth${i} 172.16.10${i}.254/24
		ip -netns ${r[1]} -6 addr add dev eth${i} 2001:db8:10${i}::64/64
	done

	ip -netns ${h[0]} nexthop add id 4 via 172.16.100.254 dev eth0
	ip -netns ${h[0]} nexthop add id 6 via 2001:db8:100::64 dev eth0

	# routing from ${h[0]} to h1-h3 and back
	for i in 1 2 3
	do
		ip -netns ${h[0]}    ro add 172.16.10${i}.0/24 nhid 4
		ip -netns ${h[$i]} ro add 172.16.100.0/24 via 172.16.10${i}.254

		ip -netns ${h[0]}    -6 ro add 2001:db8:10${i}::/64 nhid 6
		ip -netns ${h[$i]} -6 ro add 2001:db8:100::/64 via 2001:db8:10${i}::64
	done

	if [ "$VERBOSE" = "1" ]; then
		echo
		echo "host 1 config"
		ip -netns ${h[0]} li sh
		ip -netns ${h[0]} ro sh
		ip -netns ${h[0]} -6 ro sh
	fi

	#set +e
}

cleanup()
{
	cleanup_all_ns
}

change_mtu()
{
	local hostid=$1
	local mtu=$2

	run_cmd ip -netns h${hostid} li set eth0 mtu ${mtu}
	run_cmd ip -netns ${r1} li set eth${hostid} mtu ${mtu}
}

################################################################################
# validate exceptions

validate_v4_exception()
{
	local i=$1
	local mtu=$2
	local ping_sz=$3
	local dst="172.16.10${i}.1"
	local h0_ip=172.16.100.1
	local r1_ip=172.16.100.254
	local rc

	if [ ${ping_sz} != "0" ]; then
		run_cmd ip netns exec ${h0} ping -s ${ping_sz} -c5 -w5 ${dst}
	fi

	if [ "$VERBOSE" = "1" ]; then
		echo "Route get"
		ip -netns ${h0} ro get ${dst}
		echo "Searching for:"
		echo "    cache .* mtu ${mtu}"
		echo
	fi

	ip -netns ${h0} ro get ${dst} | \
	grep -q "cache .* mtu ${mtu}"
	rc=$?

	log_test $rc 0 "IPv4: host 0 to host ${i}, mtu ${mtu}"
}

validate_v6_exception()
{
	local i=$1
	local mtu=$2
	local ping_sz=$3
	local dst="2001:db8:10${i}::1"
	local h0_ip=2001:db8:100::1
	local r1_ip=2001:db8:100::64
	local rc

	if [ ${ping_sz} != "0" ]; then
		run_cmd ip netns exec ${h0} ${ping6} -s ${ping_sz} -c5 -w5 ${dst}
	fi

	if [ "$VERBOSE" = "1" ]; then
		echo "Route get"
		ip -netns ${h0} -6 ro get ${dst}
		echo "Searching for:"
		echo "    ${dst}.* via ${r1_ip} dev eth0 src ${h0_ip} .* mtu ${mtu}"
		echo
	fi

	ip -netns ${h0} -6 ro get ${dst} | \
	grep -q "${dst}.* via ${r1_ip} dev eth0 src ${h0_ip} .* mtu ${mtu}"
	rc=$?

	log_test $rc 0 "IPv6: host 0 to host ${i}, mtu ${mtu}"
}

################################################################################
# main

while getopts :pv o
do
	case $o in
		p) PAUSE_ON_FAIL=yes;;
		v) VERBOSE=1;;
	esac
done

cleanup
setup
sleep 2

cpus=$(cat  /sys/devices/system/cpu/online)
cpus="$(seq ${cpus/-/ })"
ret=0
for i in 1 2 3
do
	# generate a cached route per-cpu
	for c in ${cpus}; do
		run_cmd taskset -c ${c} ip netns exec ${h0} ping -c1 -w1 172.16.10${i}.1
		[ $? -ne 0 ] && printf "\nERROR: ping to ${h[$i]} failed\n" && ret=1

		run_cmd taskset -c ${c} ip netns exec ${h0} ${ping6} -c1 -w1 2001:db8:10${i}::1
		[ $? -ne 0 ] && printf "\nERROR: ping6 to ${h[$i]} failed\n" && ret=1

		[ $ret -ne 0 ] && break
	done
	[ $ret -ne 0 ] && break
done

if [ $ret -eq 0 ]; then
	# generate different exceptions in h0 for h1, h2 and h3
	change_mtu 1 1300
	validate_v4_exception 1 1300 1350
	validate_v6_exception 1 1300 1350
	echo

	change_mtu 2 1350
	validate_v4_exception 2 1350 1400
	validate_v6_exception 2 1350 1400
	echo

	change_mtu 3 1400
	validate_v4_exception 3 1400 1450
	validate_v6_exception 3 1400 1450
	echo

	validate_v4_exception 1 1300 0
	validate_v6_exception 1 1300 0
	echo

	validate_v4_exception 2 1350 0
	validate_v6_exception 2 1350 0
	echo

	validate_v4_exception 3 1400 0
	validate_v6_exception 3 1400 0

	# targeted deletes to trigger cleanup paths in kernel
	ip -netns ${h0} ro del 172.16.102.0/24 nhid 4
	ip -netns ${h0} -6 ro del 2001:db8:102::/64 nhid 6

	ip -netns ${h0} nexthop del id 4
	ip -netns ${h0} nexthop del id 6
fi

cleanup
