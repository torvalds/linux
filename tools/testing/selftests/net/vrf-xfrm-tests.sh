#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Various combinations of VRF with xfrms and qdisc.

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

PAUSE_ON_FAIL=no
VERBOSE=0
ret=0

HOST1_4=192.168.1.1
HOST2_4=192.168.1.2
HOST1_6=2001:db8:1::1
HOST2_6=2001:db8:1::2

XFRM1_4=10.0.1.1
XFRM2_4=10.0.1.2
XFRM1_6=fc00:1000::1
XFRM2_6=fc00:1000::2
IF_ID=123

VRF=red
TABLE=300

AUTH_1=0xd94fcfea65fddf21dc6e0d24a0253508
AUTH_2=0xdc6e0d24a0253508d94fcfea65fddf21
ENC_1=0xfc46c20f8048be9725930ff3fb07ac2a91f0347dffeacf62
ENC_2=0x3fb07ac2a91f0347dffeacf62fc46c20f8048be9725930ff
SPI_1=0x02122b77
SPI_2=0x2b770212

which ping6 > /dev/null 2>&1 && ping6=$(which ping6) || ping6=$(which ping)

################################################################################
#
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
}

run_cmd_host1()
{
	local cmd="$*"
	local out
	local rc

	if [ "$VERBOSE" = "1" ]; then
		printf "    COMMAND: $cmd\n"
	fi

	out=$(eval ip netns exec host1 $cmd 2>&1)
	rc=$?
	if [ "$VERBOSE" = "1" ]; then
		if [ -n "$out" ]; then
			echo
			echo "    $out"
		fi
		echo
	fi

	return $rc
}

################################################################################
# create namespaces for hosts and sws

create_vrf()
{
	local ns=$1
	local vrf=$2
	local table=$3

	if [ -n "${ns}" ]; then
		ns="-netns ${ns}"
	fi

	ip ${ns} link add ${vrf} type vrf table ${table}
	ip ${ns} link set ${vrf} up
	ip ${ns} route add vrf ${vrf} unreachable default metric 8192
	ip ${ns} -6 route add vrf ${vrf} unreachable default metric 8192

	ip ${ns} addr add 127.0.0.1/8 dev ${vrf}
	ip ${ns} -6 addr add ::1 dev ${vrf} nodad

	ip ${ns} ru del pref 0
	ip ${ns} ru add pref 32765 from all lookup local
	ip ${ns} -6 ru del pref 0
	ip ${ns} -6 ru add pref 32765 from all lookup local
}

create_ns()
{
	local ns=$1
	local addr=$2
	local addr6=$3

	[ -z "${addr}" ] && addr="-"
	[ -z "${addr6}" ] && addr6="-"

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
	ip netns exec ${ns} sysctl -qw net.ipv6.conf.default.accept_dad=0
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
	local ns1arg
	local ns2arg

	if [ -n "${ns1}" ]; then
		ns1arg="-netns ${ns1}"
	fi
	if [ -n "${ns2}" ]; then
		ns2arg="-netns ${ns2}"
	fi

	ip ${ns1arg} li add ${ns1_dev} type veth peer name tmp
	ip ${ns1arg} li set ${ns1_dev} up
	ip ${ns1arg} li set tmp netns ${ns2} name ${ns2_dev}
	ip ${ns2arg} li set ${ns2_dev} up

	if [ "${ns1_addr}" != "-" ]; then
		ip ${ns1arg} addr add dev ${ns1_dev} ${ns1_addr}
		ip ${ns2arg} addr add dev ${ns2_dev} ${ns2_addr}
	fi

	if [ "${ns1_addr6}" != "-" ]; then
		ip ${ns1arg} addr add dev ${ns1_dev} ${ns1_addr6} nodad
		ip ${ns2arg} addr add dev ${ns2_dev} ${ns2_addr6} nodad
	fi
}

################################################################################

cleanup()
{
	ip netns del host1
	ip netns del host2
}

setup()
{
	create_ns "host1"
	create_ns "host2"

	connect_ns "host1" eth0 ${HOST1_4}/24 ${HOST1_6}/64 \
	           "host2" eth0 ${HOST2_4}/24 ${HOST2_6}/64

	create_vrf "host1" ${VRF} ${TABLE}
	ip -netns host1 link set dev eth0 master ${VRF}
}

cleanup_xfrm()
{
	for ns in host1 host2
	do
		for x in state policy
		do
			ip -netns ${ns} xfrm ${x} flush
			ip -6 -netns ${ns} xfrm ${x} flush
		done
	done
}

setup_xfrm()
{
	local h1_4=$1
	local h2_4=$2
	local h1_6=$3
	local h2_6=$4
	local devarg="$5"

	#
	# policy
	#

	# host1 - IPv4 out
	ip -netns host1 xfrm policy add \
	  src ${h1_4} dst ${h2_4} ${devarg} dir out \
	  tmpl src ${HOST1_4} dst ${HOST2_4} proto esp mode tunnel

	# host2 - IPv4 in
	ip -netns host2 xfrm policy add \
	  src ${h1_4} dst ${h2_4} dir in \
	  tmpl src ${HOST1_4} dst ${HOST2_4} proto esp mode tunnel

	# host1 - IPv4 in
	ip -netns host1 xfrm policy add \
	  src ${h2_4} dst ${h1_4} ${devarg} dir in \
	  tmpl src ${HOST2_4} dst ${HOST1_4} proto esp mode tunnel

	# host2 - IPv4 out
	ip -netns host2 xfrm policy add \
	  src ${h2_4} dst ${h1_4} dir out \
	  tmpl src ${HOST2_4} dst ${HOST1_4} proto esp mode tunnel


	# host1 - IPv6 out
	ip -6 -netns host1 xfrm policy add \
	  src ${h1_6} dst ${h2_6} ${devarg} dir out \
	  tmpl src ${HOST1_6} dst ${HOST2_6} proto esp mode tunnel

	# host2 - IPv6 in
	ip -6 -netns host2 xfrm policy add \
	  src ${h1_6} dst ${h2_6} dir in \
	  tmpl src ${HOST1_6} dst ${HOST2_6} proto esp mode tunnel

	# host1 - IPv6 in
	ip -6 -netns host1 xfrm policy add \
	  src ${h2_6} dst ${h1_6} ${devarg} dir in \
	  tmpl src ${HOST2_6} dst ${HOST1_6} proto esp mode tunnel

	# host2 - IPv6 out
	ip -6 -netns host2 xfrm policy add \
	  src ${h2_6} dst ${h1_6} dir out \
	  tmpl src ${HOST2_6} dst ${HOST1_6} proto esp mode tunnel

	#
	# state
	#
	ip -netns host1 xfrm state add src ${HOST1_4} dst ${HOST2_4} \
	    proto esp spi ${SPI_1} reqid 0 mode tunnel \
	    replay-window 4 replay-oseq 0x4 \
	    auth-trunc 'hmac(sha1)' ${AUTH_1} 96 \
	    enc 'cbc(aes)' ${ENC_1} \
	    sel src ${h1_4} dst ${h2_4} ${devarg}

	ip -netns host2 xfrm state add src ${HOST1_4} dst ${HOST2_4} \
	    proto esp spi ${SPI_1} reqid 0 mode tunnel \
	    replay-window 4 replay-oseq 0x4 \
	    auth-trunc 'hmac(sha1)' ${AUTH_1} 96 \
	    enc 'cbc(aes)' ${ENC_1} \
	    sel src ${h1_4} dst ${h2_4}


	ip -netns host1 xfrm state add src ${HOST2_4} dst ${HOST1_4} \
	    proto esp spi ${SPI_2} reqid 0 mode tunnel \
	    replay-window 4 replay-oseq 0x4 \
	    auth-trunc 'hmac(sha1)' ${AUTH_2} 96 \
	    enc 'cbc(aes)' ${ENC_2} \
	    sel src ${h2_4} dst ${h1_4} ${devarg}

	ip -netns host2 xfrm state add src ${HOST2_4} dst ${HOST1_4} \
	    proto esp spi ${SPI_2} reqid 0 mode tunnel \
	    replay-window 4 replay-oseq 0x4 \
	    auth-trunc 'hmac(sha1)' ${AUTH_2} 96 \
	    enc 'cbc(aes)' ${ENC_2} \
	    sel src ${h2_4} dst ${h1_4}


	ip -6 -netns host1 xfrm state add src ${HOST1_6} dst ${HOST2_6} \
	    proto esp spi ${SPI_1} reqid 0 mode tunnel \
	    replay-window 4 replay-oseq 0x4 \
	    auth-trunc 'hmac(sha1)' ${AUTH_1} 96 \
	    enc 'cbc(aes)' ${ENC_1} \
	    sel src ${h1_6} dst ${h2_6} ${devarg}

	ip -6 -netns host2 xfrm state add src ${HOST1_6} dst ${HOST2_6} \
	    proto esp spi ${SPI_1} reqid 0 mode tunnel \
	    replay-window 4 replay-oseq 0x4 \
	    auth-trunc 'hmac(sha1)' ${AUTH_1} 96 \
	    enc 'cbc(aes)' ${ENC_1} \
	    sel src ${h1_6} dst ${h2_6}


	ip -6 -netns host1 xfrm state add src ${HOST2_6} dst ${HOST1_6} \
	    proto esp spi ${SPI_2} reqid 0 mode tunnel \
	    replay-window 4 replay-oseq 0x4 \
	    auth-trunc 'hmac(sha1)' ${AUTH_2} 96 \
	    enc 'cbc(aes)' ${ENC_2} \
	    sel src ${h2_6} dst ${h1_6} ${devarg}

	ip -6 -netns host2 xfrm state add src ${HOST2_6} dst ${HOST1_6} \
	    proto esp spi ${SPI_2} reqid 0 mode tunnel \
	    replay-window 4 replay-oseq 0x4 \
	    auth-trunc 'hmac(sha1)' ${AUTH_2} 96 \
	    enc 'cbc(aes)' ${ENC_2} \
	    sel src ${h2_6} dst ${h1_6}
}

cleanup_xfrm_dev()
{
	ip -netns host1 li del xfrm0
	ip -netns host2 addr del ${XFRM2_4}/24 dev eth0
	ip -netns host2 addr del ${XFRM2_6}/64 dev eth0
}

setup_xfrm_dev()
{
	local vrfarg="vrf ${VRF}"

	ip -netns host1 li add type xfrm dev eth0 if_id ${IF_ID}
	ip -netns host1 li set xfrm0 ${vrfarg} up
	ip -netns host1 addr add ${XFRM1_4}/24 dev xfrm0
	ip -netns host1 addr add ${XFRM1_6}/64 dev xfrm0

	ip -netns host2 addr add ${XFRM2_4}/24 dev eth0
	ip -netns host2 addr add ${XFRM2_6}/64 dev eth0

	setup_xfrm ${XFRM1_4} ${XFRM2_4} ${XFRM1_6} ${XFRM2_6} "if_id ${IF_ID}"
}

run_tests()
{
	cleanup_xfrm

	# no IPsec
	run_cmd_host1 ip vrf exec ${VRF} ping -c1 -w1 ${HOST2_4}
	log_test $? 0 "IPv4 no xfrm policy"
	run_cmd_host1 ip vrf exec ${VRF} ${ping6} -c1 -w1 ${HOST2_6}
	log_test $? 0 "IPv6 no xfrm policy"

	# xfrm without VRF in sel
	setup_xfrm ${HOST1_4} ${HOST2_4} ${HOST1_6} ${HOST2_6}
	run_cmd_host1 ip vrf exec ${VRF} ping -c1 -w1 ${HOST2_4}
	log_test $? 0 "IPv4 xfrm policy based on address"
	run_cmd_host1 ip vrf exec ${VRF} ${ping6} -c1 -w1 ${HOST2_6}
	log_test $? 0 "IPv6 xfrm policy based on address"
	cleanup_xfrm

	# xfrm with VRF in sel
	# Known failure: ipv4 resets the flow oif after the lookup. Fix is
	# not straightforward.
	# setup_xfrm ${HOST1_4} ${HOST2_4} ${HOST1_6} ${HOST2_6} "dev ${VRF}"
	# run_cmd_host1 ip vrf exec ${VRF} ping -c1 -w1 ${HOST2_4}
	# log_test $? 0 "IPv4 xfrm policy with VRF in selector"
	run_cmd_host1 ip vrf exec ${VRF} ${ping6} -c1 -w1 ${HOST2_6}
	log_test $? 0 "IPv6 xfrm policy with VRF in selector"
	cleanup_xfrm

	# xfrm with enslaved device in sel
	# Known failures: combined with the above, __xfrm{4,6}_selector_match
	# needs to consider both l3mdev and enslaved device index.
	# setup_xfrm ${HOST1_4} ${HOST2_4} ${HOST1_6} ${HOST2_6} "dev eth0"
	# run_cmd_host1 ip vrf exec ${VRF} ping -c1 -w1 ${HOST2_4}
	# log_test $? 0 "IPv4 xfrm policy with enslaved device in selector"
	# run_cmd_host1 ip vrf exec ${VRF} ${ping6} -c1 -w1 ${HOST2_6}
	# log_test $? 0 "IPv6 xfrm policy with enslaved device in selector"
	# cleanup_xfrm

	# xfrm device
	setup_xfrm_dev
	run_cmd_host1 ip vrf exec ${VRF} ping -c1 -w1 ${XFRM2_4}
	log_test $? 0 "IPv4 xfrm policy with xfrm device"
	run_cmd_host1 ip vrf exec ${VRF} ${ping6} -c1 -w1 ${XFRM2_6}
	log_test $? 0 "IPv6 xfrm policy with xfrm device"
	cleanup_xfrm_dev
}

################################################################################
# usage

usage()
{
        cat <<EOF
usage: ${0##*/} OPTS

        -p          Pause on fail
        -v          verbose mode (show commands and output)

done
EOF
}

################################################################################
# main

while getopts :pv o
do
	case $o in
		p) PAUSE_ON_FAIL=yes;;
		v) VERBOSE=$(($VERBOSE + 1));;
		h) usage; exit 0;;
		*) usage; exit 1;;
	esac
done

cleanup 2>/dev/null
setup

echo
echo "No qdisc on VRF device"
run_tests

run_cmd_host1 tc qdisc add dev ${VRF} root netem delay 100ms
echo
echo "netem qdisc on VRF device"
run_tests

printf "\nTests passed: %3d\n" ${nsuccess}
printf "Tests failed: %3d\n"   ${nfail}

exit $ret
