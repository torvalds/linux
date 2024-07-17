#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# nft_concat_range.sh - Tests for sets with concatenation of ranged fields
#
# Copyright (c) 2019 Red Hat GmbH
#
# Author: Stefano Brivio <sbrivio@redhat.com>
#
# shellcheck disable=SC2154,SC2034,SC2016,SC2030,SC2031,SC2317
# ^ Configuration and templates sourced with eval, counters reused in subshells

source lib.sh

# Available test groups:
# - reported_issues: check for issues that were reported in the past
# - correctness: check that packets match given entries, and only those
# - concurrency: attempt races between insertion, deletion and lookup
# - timeout: check that packets match entries until they expire
# - performance: estimate matching rate, compare with rbtree and hash baselines
TESTS="reported_issues correctness concurrency timeout"
[ -n "$NFT_CONCAT_RANGE_TESTS" ] && TESTS="${NFT_CONCAT_RANGE_TESTS}"

# Set types, defined by TYPE_ variables below
TYPES="net_port port_net net6_port port_proto net6_port_mac net6_port_mac_proto
       net_port_net net_mac mac_net net_mac_icmp net6_mac_icmp
       net6_port_net6_port net_port_mac_proto_net"

# Reported bugs, also described by TYPE_ variables below
BUGS="flush_remove_add reload"

# List of possible paths to pktgen script from kernel tree for performance tests
PKTGEN_SCRIPT_PATHS="
	../../../../../samples/pktgen/pktgen_bench_xmit_mode_netif_receive.sh
	pktgen/pktgen_bench_xmit_mode_netif_receive.sh"

# Definition of set types:
# display	display text for test report
# type_spec	nftables set type specifier
# chain_spec	nftables type specifier for rules mapping to set
# dst		call sequence of format_*() functions for destination fields
# src		call sequence of format_*() functions for source fields
# start		initial integer used to generate addresses and ports
# count		count of entries to generate and match
# src_delta	number summed to destination generator for source fields
# tools		list of tools for correctness and timeout tests, any can be used
# proto		L4 protocol of test packets
#
# race_repeat	race attempts per thread, 0 disables concurrency test for type
# flood_tools	list of tools for concurrency tests, any can be used
# flood_proto	L4 protocol of test packets for concurrency tests
# flood_spec	nftables type specifier for concurrency tests
#
# perf_duration	duration of single pktgen injection test
# perf_spec	nftables type specifier for performance tests
# perf_dst	format_*() functions for destination fields in performance test
# perf_src	format_*() functions for source fields in performance test
# perf_entries	number of set entries for performance test
# perf_proto	L3 protocol of test packets
TYPE_net_port="
display		net,port
type_spec	ipv4_addr . inet_service
chain_spec	ip daddr . udp dport
dst		addr4 port
src		 
start		1
count		5
src_delta	2000
tools		sendip bash
proto		udp

race_repeat	3
flood_tools	iperf3 iperf netperf
flood_proto	udp
flood_spec	ip daddr . udp dport

perf_duration	5
perf_spec	ip daddr . udp dport
perf_dst	addr4 port
perf_src	 
perf_entries	1000
perf_proto	ipv4
"

TYPE_port_net="
display		port,net
type_spec	inet_service . ipv4_addr
chain_spec	udp dport . ip daddr
dst		port addr4
src		 
start		1
count		5
src_delta	2000
tools		sendip socat bash
proto		udp

race_repeat	3
flood_tools	iperf3 iperf netperf
flood_proto	udp
flood_spec	udp dport . ip daddr

perf_duration	5
perf_spec	udp dport . ip daddr
perf_dst	port addr4
perf_src	 
perf_entries	100
perf_proto	ipv4
"

TYPE_net6_port="
display		net6,port
type_spec	ipv6_addr . inet_service
chain_spec	ip6 daddr . udp dport
dst		addr6 port
src		 
start		10
count		5
src_delta	2000
tools		sendip socat bash
proto		udp6

race_repeat	3
flood_tools	iperf3 iperf netperf
flood_proto	tcp6
flood_spec	ip6 daddr . udp dport

perf_duration	5
perf_spec	ip6 daddr . udp dport
perf_dst	addr6 port
perf_src	 
perf_entries	1000
perf_proto	ipv6
"

TYPE_port_proto="
display		port,proto
type_spec	inet_service . inet_proto
chain_spec	udp dport . meta l4proto
dst		port proto
src		 
start		1
count		5
src_delta	2000
tools		sendip socat bash
proto		udp

race_repeat	0

perf_duration	5
perf_spec	udp dport . meta l4proto
perf_dst	port proto
perf_src	 
perf_entries	30000
perf_proto	ipv4
"

TYPE_net6_port_mac="
display		net6,port,mac
type_spec	ipv6_addr . inet_service . ether_addr
chain_spec	ip6 daddr . udp dport . ether saddr
dst		addr6 port
src		mac
start		10
count		5
src_delta	2000
tools		sendip socat bash
proto		udp6

race_repeat	0

perf_duration	5
perf_spec	ip6 daddr . udp dport . ether daddr
perf_dst	addr6 port mac
perf_src	 
perf_entries	10
perf_proto	ipv6
"

TYPE_net6_port_mac_proto="
display		net6,port,mac,proto
type_spec	ipv6_addr . inet_service . ether_addr . inet_proto
chain_spec	ip6 daddr . udp dport . ether saddr . meta l4proto
dst		addr6 port
src		mac proto
start		10
count		5
src_delta	2000
tools		sendip socat bash
proto		udp6

race_repeat	0

perf_duration	5
perf_spec	ip6 daddr . udp dport . ether daddr . meta l4proto
perf_dst	addr6 port mac proto
perf_src	 
perf_entries	1000
perf_proto	ipv6
"

TYPE_net_port_net="
display		net,port,net
type_spec	ipv4_addr . inet_service . ipv4_addr
chain_spec	ip daddr . udp dport . ip saddr
dst		addr4 port
src		addr4
start		1
count		5
src_delta	2000
tools		sendip socat bash
proto		udp

race_repeat	3
flood_tools	iperf3 iperf netperf
flood_proto	tcp
flood_spec	ip daddr . udp dport . ip saddr

perf_duration	0
"

TYPE_net6_port_net6_port="
display		net6,port,net6,port
type_spec	ipv6_addr . inet_service . ipv6_addr . inet_service
chain_spec	ip6 daddr . udp dport . ip6 saddr . udp sport
dst		addr6 port
src		addr6 port
start		10
count		5
src_delta	2000
tools		sendip socat
proto		udp6

race_repeat	3
flood_tools	iperf3 iperf netperf
flood_proto	tcp6
flood_spec	ip6 daddr . tcp dport . ip6 saddr . tcp sport

perf_duration	0
"

TYPE_net_port_mac_proto_net="
display		net,port,mac,proto,net
type_spec	ipv4_addr . inet_service . ether_addr . inet_proto . ipv4_addr
chain_spec	ip daddr . udp dport . ether saddr . meta l4proto . ip saddr
dst		addr4 port
src		mac proto addr4
start		1
count		5
src_delta	2000
tools		sendip socat bash
proto		udp

race_repeat	0

perf_duration	0
"

TYPE_net_mac="
display		net,mac
type_spec	ipv4_addr . ether_addr
chain_spec	ip daddr . ether saddr
dst		addr4
src		mac
start		1
count		5
src_delta	2000
tools		sendip socat bash
proto		udp

race_repeat	0

perf_duration	5
perf_spec	ip daddr . ether daddr
perf_dst	addr4 mac
perf_src	 
perf_entries	1000
perf_proto	ipv4
"

TYPE_mac_net="
display		mac,net
type_spec	ether_addr . ipv4_addr
chain_spec	ether saddr . ip saddr
dst		 
src		mac addr4
start		1
count		5
src_delta	2000
tools		sendip socat bash
proto		udp

race_repeat	0

perf_duration	0
"

TYPE_net_mac_icmp="
display		net,mac - ICMP
type_spec	ipv4_addr . ether_addr
chain_spec	ip daddr . ether saddr
dst		addr4
src		mac
start		1
count		5
src_delta	2000
tools		ping
proto		icmp

race_repeat	0

perf_duration	0
"

TYPE_net6_mac_icmp="
display		net6,mac - ICMPv6
type_spec	ipv6_addr . ether_addr
chain_spec	ip6 daddr . ether saddr
dst		addr6
src		mac
start		10
count		50
src_delta	2000
tools		ping
proto		icmp6

race_repeat	0

perf_duration	0
"

TYPE_net_port_proto_net="
display		net,port,proto,net
type_spec	ipv4_addr . inet_service . inet_proto . ipv4_addr
chain_spec	ip daddr . udp dport . meta l4proto . ip saddr
dst		addr4 port proto
src		addr4
start		1
count		5
src_delta	2000
tools		sendip socat
proto		udp

race_repeat	3
flood_tools	iperf3 iperf netperf
flood_proto	tcp
flood_spec	ip daddr . tcp dport . meta l4proto . ip saddr

perf_duration	0
"

# Definition of tests for bugs reported in the past:
# display	display text for test report
TYPE_flush_remove_add="
display		Add two elements, flush, re-add
"

TYPE_reload="
display		net,mac with reload
type_spec	ipv4_addr . ether_addr
chain_spec	ip daddr . ether saddr
dst		addr4
src		mac
start		1
count		1
src_delta	2000
tools		sendip socat bash
proto		udp

race_repeat	0

perf_duration	0
"

# Set template for all tests, types and rules are filled in depending on test
set_template='
flush ruleset

table inet filter {
	counter test {
		packets 0 bytes 0
	}

	set test {
		type ${type_spec}
		flags interval,timeout
	}

	chain input {
		type filter hook prerouting priority 0; policy accept;
		${chain_spec} @test counter name \"test\"
	}
}

table netdev perf {
	counter test {
		packets 0 bytes 0
	}

	counter match {
		packets 0 bytes 0
	}

	set test {
		type ${type_spec}
		flags interval
	}

	set norange {
		type ${type_spec}
	}

	set noconcat {
		type ${type_spec%% *}
		flags interval
	}

	chain test {
		type filter hook ingress device veth_a priority 0;
	}
}
'

err_buf=
info_buf=

# Append string to error buffer
err() {
	err_buf="${err_buf}${1}
"
}

# Append string to information buffer
info() {
	info_buf="${info_buf}${1}
"
}

# Flush error buffer to stdout
err_flush() {
	printf "%s" "${err_buf}"
	err_buf=
}

# Flush information buffer to stdout
info_flush() {
	printf "%s" "${info_buf}"
	info_buf=
}

# Setup veth pair: this namespace receives traffic, B generates it
setup_veth() {
	ip netns add B
	ip link add veth_a type veth peer name veth_b || return 1

	ip link set veth_a up
	ip link set veth_b netns B

	ip -n B link set veth_b up

	ip addr add dev veth_a 10.0.0.1
	ip route add default dev veth_a

	ip -6 addr add fe80::1/64 dev veth_a nodad
	ip -6 addr add 2001:db8::1/64 dev veth_a nodad
	ip -6 route add default dev veth_a

	ip -n B route add default dev veth_b

	ip -6 -n B addr add fe80::2/64 dev veth_b nodad
	ip -6 -n B addr add 2001:db8::2/64 dev veth_b nodad
	ip -6 -n B route add default dev veth_b

	B() {
		ip netns exec B "$@" >/dev/null 2>&1
	}
}

# Fill in set template and initialise set
setup_set() {
	eval "echo \"${set_template}\"" | nft -f -
}

# Check that at least one of the needed tools is available
check_tools() {
	[ -z "${tools}" ] && return 0

	__tools=
	for tool in ${tools}; do
		__tools="${__tools} ${tool}"

		command -v "${tool}" >/dev/null && return 0
	done
	err "need one of:${__tools}, skipping" && return 1
}

# Set up function to send ICMP packets
setup_send_icmp() {
	send_icmp() {
		B ping -c1 -W1 "${dst_addr4}" >/dev/null 2>&1
	}
}

# Set up function to send ICMPv6 packets
setup_send_icmp6() {
	if command -v ping6 >/dev/null; then
		send_icmp6() {
			ip -6 addr add "${dst_addr6}" dev veth_a nodad \
				2>/dev/null
			B ping6 -q -c1 -W1 "${dst_addr6}"
		}
	else
		send_icmp6() {
			ip -6 addr add "${dst_addr6}" dev veth_a nodad \
				2>/dev/null
			B ping -q -6 -c1 -W1 "${dst_addr6}"
		}
	fi
}

# Set up function to send single UDP packets on IPv4
setup_send_udp() {
	if command -v sendip >/dev/null; then
		send_udp() {
			[ -n "${src_port}" ] && src_port="-us ${src_port}"
			[ -n "${dst_port}" ] && dst_port="-ud ${dst_port}"
			[ -n "${src_addr4}" ] && src_addr4="-is ${src_addr4}"

			# shellcheck disable=SC2086 # sendip needs split options
			B sendip -p ipv4 -p udp ${src_addr4} ${src_port} \
						${dst_port} "${dst_addr4}"

			src_port=
			dst_port=
			src_addr4=
		}
	elif command -v socat -v >/dev/null; then
		send_udp() {
			if [ -n "${src_addr4}" ]; then
				B ip addr add "${src_addr4}" dev veth_b
				__socatbind=",bind=${src_addr4}"
				if [ -n "${src_port}" ];then
					__socatbind="${__socatbind}:${src_port}"
				fi
			fi

			ip addr add "${dst_addr4}" dev veth_a 2>/dev/null
			[ -z "${dst_port}" ] && dst_port=12345

			echo "test4" | B socat -t 0.01 STDIN UDP4-DATAGRAM:"$dst_addr4":"$dst_port""${__socatbind}"

			src_addr4=
			src_port=
		}
	elif [ -z "$(bash -c 'type -p')" ]; then
		send_udp() {
			ip addr add "${dst_addr4}" dev veth_a 2>/dev/null
			if [ -n "${src_addr4}" ]; then
				B ip addr add "${src_addr4}/16" dev veth_b
				B ip route add default dev veth_b
			fi

			B bash -c "echo > /dev/udp/${dst_addr4}/${dst_port}"

			if [ -n "${src_addr4}" ]; then
				B ip addr del "${src_addr4}/16" dev veth_b
			fi
			src_addr4=
		}
	else
		return 1
	fi
}

# Set up function to send single UDP packets on IPv6
setup_send_udp6() {
	if command -v sendip >/dev/null; then
		send_udp6() {
			[ -n "${src_port}" ] && src_port="-us ${src_port}"
			[ -n "${dst_port}" ] && dst_port="-ud ${dst_port}"
			if [ -n "${src_addr6}" ]; then
				src_addr6="-6s ${src_addr6}"
			else
				src_addr6="-6s 2001:db8::2"
			fi
			ip -6 addr add "${dst_addr6}" dev veth_a nodad \
				2>/dev/null

			# shellcheck disable=SC2086 # this needs split options
			B sendip -p ipv6 -p udp ${src_addr6} ${src_port} \
						${dst_port} "${dst_addr6}"

			src_port=
			dst_port=
			src_addr6=
		}
	elif command -v socat -v >/dev/null; then
		send_udp6() {
			ip -6 addr add "${dst_addr6}" dev veth_a nodad \
				2>/dev/null

			__socatbind6=

			if [ -n "${src_addr6}" ]; then
				B ip addr add "${src_addr6}" dev veth_b nodad

				__socatbind6=",bind=[${src_addr6}]"

				if [ -n "${src_port}" ] ;then
					__socatbind6="${__socatbind6}:${src_port}"
				fi
			fi

			echo "test6" | B socat -t 0.01 STDIN UDP6-DATAGRAM:["$dst_addr6"]:"$dst_port""${__socatbind6}"
		}
	elif [ -z "$(bash -c 'type -p')" ]; then
		send_udp6() {
			ip -6 addr add "${dst_addr6}" dev veth_a nodad \
				2>/dev/null
			B ip addr add "${src_addr6}" dev veth_b nodad
			B bash -c "echo > /dev/udp/${dst_addr6}/${dst_port}"
			ip -6 addr del "${dst_addr6}" dev veth_a 2>/dev/null
		}
	else
		return 1
	fi
}

listener_ready()
{
	port="$1"
	ss -lnt -o "sport = :$port" | grep -q "$port"
}

# Set up function to send TCP traffic on IPv4
setup_flood_tcp() {
	if command -v iperf3 >/dev/null; then
		flood_tcp() {
			local n_port="${dst_port}"
			[ -n "${dst_port}" ] && dst_port="-p ${dst_port}"
			if [ -n "${src_addr4}" ]; then
				B ip addr add "${src_addr4}/16" dev veth_b
				src_addr4="-B ${src_addr4}"
			else
				B ip addr add dev veth_b 10.0.0.2
				src_addr4="-B 10.0.0.2"
			fi
			if [ -n "${src_port}" ]; then
				src_port="--cport ${src_port}"
			fi
			B ip route add default dev veth_b 2>/dev/null
			ip addr add "${dst_addr4}" dev veth_a 2>/dev/null

			# shellcheck disable=SC2086 # this needs split options
			iperf3 -s -DB "${dst_addr4}" ${dst_port} >/dev/null 2>&1
			busywait "$BUSYWAIT_TIMEOUT" listener_ready "$n_port"

			# shellcheck disable=SC2086 # this needs split options
			B iperf3 -c "${dst_addr4}" ${dst_port} ${src_port} \
				${src_addr4} -l16 -t 1000

			src_addr4=
			src_port=
			dst_port=
		}
	elif command -v iperf >/dev/null; then
		flood_tcp() {
			local n_port="${dst_port}"
			[ -n "${dst_port}" ] && dst_port="-p ${dst_port}"
			if [ -n "${src_addr4}" ]; then
				B ip addr add "${src_addr4}/16" dev veth_b
				src_addr4="-B ${src_addr4}"
			else
				B ip addr add dev veth_b 10.0.0.2 2>/dev/null
				src_addr4="-B 10.0.0.2"
			fi
			if [ -n "${src_port}" ]; then
				src_addr4="${src_addr4}:${src_port}"
			fi
			B ip route add default dev veth_b
			ip addr add "${dst_addr4}" dev veth_a 2>/dev/null

			# shellcheck disable=SC2086 # this needs split options
			iperf -s -DB "${dst_addr4}" ${dst_port} >/dev/null 2>&1
			busywait "$BUSYWAIT_TIMEOUT" listener_ready "$n_port"

			# shellcheck disable=SC2086 # this needs split options
			B iperf -c "${dst_addr4}" ${dst_port} ${src_addr4} \
				-l20 -t 1000

			src_addr4=
			src_port=
			dst_port=
		}
	elif command -v netperf >/dev/null; then
		flood_tcp() {
			local n_port="${dst_port}"
			[ -n "${dst_port}" ] && dst_port="-p ${dst_port}"
			if [ -n "${src_addr4}" ]; then
				B ip addr add "${src_addr4}/16" dev veth_b
			else
				B ip addr add dev veth_b 10.0.0.2
				src_addr4="10.0.0.2"
			fi
			if [ -n "${src_port}" ]; then
				dst_port="${dst_port},${src_port}"
			fi
			B ip route add default dev veth_b
			ip addr add "${dst_addr4}" dev veth_a 2>/dev/null

			# shellcheck disable=SC2086 # this needs split options
			netserver -4 ${dst_port} -L "${dst_addr4}" \
				>/dev/null 2>&1
			busywait "$BUSYWAIT_TIMEOUT" listener_ready "${n_port}"

			# shellcheck disable=SC2086 # this needs split options
			B netperf -4 -H "${dst_addr4}" ${dst_port} \
				-L "${src_addr4}" -l 1000 -t TCP_STREAM

			src_addr4=
			src_port=
			dst_port=
		}
	else
		return 1
	fi
}

# Set up function to send TCP traffic on IPv6
setup_flood_tcp6() {
	if command -v iperf3 >/dev/null; then
		flood_tcp6() {
			local n_port="${dst_port}"
			[ -n "${dst_port}" ] && dst_port="-p ${dst_port}"
			if [ -n "${src_addr6}" ]; then
				B ip addr add "${src_addr6}" dev veth_b nodad
				src_addr6="-B ${src_addr6}"
			else
				src_addr6="-B 2001:db8::2"
			fi
			if [ -n "${src_port}" ]; then
				src_port="--cport ${src_port}"
			fi
			B ip route add default dev veth_b
			ip -6 addr add "${dst_addr6}" dev veth_a nodad \
				2>/dev/null

			# shellcheck disable=SC2086 # this needs split options
			iperf3 -s -DB "${dst_addr6}" ${dst_port} >/dev/null 2>&1
			busywait "$BUSYWAIT_TIMEOUT" listener_ready "${n_port}"

			# shellcheck disable=SC2086 # this needs split options
			B iperf3 -c "${dst_addr6}" ${dst_port} \
				${src_port} ${src_addr6} -l16 -t 1000

			src_addr6=
			src_port=
			dst_port=
		}
	elif command -v iperf >/dev/null; then
		flood_tcp6() {
			local n_port="${dst_port}"
			[ -n "${dst_port}" ] && dst_port="-p ${dst_port}"
			if [ -n "${src_addr6}" ]; then
				B ip addr add "${src_addr6}" dev veth_b nodad
				src_addr6="-B ${src_addr6}"
			else
				src_addr6="-B 2001:db8::2"
			fi
			if [ -n "${src_port}" ]; then
				src_addr6="${src_addr6}:${src_port}"
			fi
			B ip route add default dev veth_b
			ip -6 addr add "${dst_addr6}" dev veth_a nodad \
				2>/dev/null

			# shellcheck disable=SC2086 # this needs split options
			iperf -s -VDB "${dst_addr6}" ${dst_port} >/dev/null 2>&1
			busywait "$BUSYWAIT_TIMEOUT" listener_ready "$n_port"

			# shellcheck disable=SC2086 # this needs split options
			B iperf -c "${dst_addr6}" -V ${dst_port} \
				${src_addr6} -l1 -t 1000

			src_addr6=
			src_port=
			dst_port=
		}
	elif command -v netperf >/dev/null; then
		flood_tcp6() {
			local n_port="${dst_port}"
			[ -n "${dst_port}" ] && dst_port="-p ${dst_port}"
			if [ -n "${src_addr6}" ]; then
				B ip addr add "${src_addr6}" dev veth_b nodad
			else
				src_addr6="2001:db8::2"
			fi
			if [ -n "${src_port}" ]; then
				dst_port="${dst_port},${src_port}"
			fi
			B ip route add default dev veth_b
			ip -6 addr add "${dst_addr6}" dev veth_a nodad \
				2>/dev/null

			# shellcheck disable=SC2086 # this needs split options
			netserver -6 ${dst_port} -L "${dst_addr6}" \
				>/dev/null 2>&1
			busywait "$BUSYWAIT_TIMEOUT" listener_ready "$n_port"

			# shellcheck disable=SC2086 # this needs split options
			B netperf -6 -H "${dst_addr6}" ${dst_port} \
				-L "${src_addr6}" -l 1000 -t TCP_STREAM

			src_addr6=
			src_port=
			dst_port=
		}
	else
		return 1
	fi
}

# Set up function to send UDP traffic on IPv4
setup_flood_udp() {
	if command -v iperf3 >/dev/null; then
		flood_udp() {
			local n_port="${dst_port}"
			[ -n "${dst_port}" ] && dst_port="-p ${dst_port}"
			if [ -n "${src_addr4}" ]; then
				B ip addr add "${src_addr4}/16" dev veth_b
				src_addr4="-B ${src_addr4}"
			else
				B ip addr add dev veth_b 10.0.0.2 2>/dev/null
				src_addr4="-B 10.0.0.2"
			fi
			if [ -n "${src_port}" ]; then
				src_port="--cport ${src_port}"
			fi
			B ip route add default dev veth_b
			ip addr add "${dst_addr4}" dev veth_a 2>/dev/null

			# shellcheck disable=SC2086 # this needs split options
			iperf3 -s -DB "${dst_addr4}" ${dst_port}
			busywait "$BUSYWAIT_TIMEOUT" listener_ready "$n_port"

			# shellcheck disable=SC2086 # this needs split options
			B iperf3 -u -c "${dst_addr4}" -Z -b 100M -l16 -t1000 \
				${dst_port} ${src_port} ${src_addr4}

			src_addr4=
			src_port=
			dst_port=
		}
	elif command -v iperf >/dev/null; then
		flood_udp() {
			local n_port="${dst_port}"
			[ -n "${dst_port}" ] && dst_port="-p ${dst_port}"
			if [ -n "${src_addr4}" ]; then
				B ip addr add "${src_addr4}/16" dev veth_b
				src_addr4="-B ${src_addr4}"
			else
				B ip addr add dev veth_b 10.0.0.2
				src_addr4="-B 10.0.0.2"
			fi
			if [ -n "${src_port}" ]; then
				src_addr4="${src_addr4}:${src_port}"
			fi
			B ip route add default dev veth_b
			ip addr add "${dst_addr4}" dev veth_a 2>/dev/null

			# shellcheck disable=SC2086 # this needs split options
			iperf -u -sDB "${dst_addr4}" ${dst_port} >/dev/null 2>&1
			busywait "$BUSYWAIT_TIMEOUT" listener_ready "$n_port"

			# shellcheck disable=SC2086 # this needs split options
			B iperf -u -c "${dst_addr4}" -b 100M -l1 -t1000 \
				${dst_port} ${src_addr4}

			src_addr4=
			src_port=
			dst_port=
		}
	elif command -v netperf >/dev/null; then
		flood_udp() {
			local n_port="${dst_port}"
			[ -n "${dst_port}" ] && dst_port="-p ${dst_port}"
			if [ -n "${src_addr4}" ]; then
				B ip addr add "${src_addr4}/16" dev veth_b
			else
				B ip addr add dev veth_b 10.0.0.2
				src_addr4="10.0.0.2"
			fi
			if [ -n "${src_port}" ]; then
				dst_port="${dst_port},${src_port}"
			fi
			B ip route add default dev veth_b
			ip addr add "${dst_addr4}" dev veth_a 2>/dev/null

			# shellcheck disable=SC2086 # this needs split options
			netserver -4 ${dst_port} -L "${dst_addr4}" \
				>/dev/null 2>&1
			busywait "$BUSYWAIT_TIMEOUT" listener_ready "$n_port"

			# shellcheck disable=SC2086 # this needs split options
			B netperf -4 -H "${dst_addr4}" ${dst_port} \
				-L "${src_addr4}" -l 1000 -t UDP_STREAM

			src_addr4=
			src_port=
			dst_port=
		}
	else
		return 1
	fi
}

# Find pktgen script and set up function to start pktgen injection
setup_perf() {
	for pktgen_script_path in ${PKTGEN_SCRIPT_PATHS} __notfound; do
		command -v "${pktgen_script_path}" >/dev/null && break
	done
	[ "${pktgen_script_path}" = "__notfound" ] && return 1

	perf_ipv4() {
		${pktgen_script_path} -s80 \
			-i veth_a -d "${dst_addr4}" -p "${dst_port}" \
			-m "${dst_mac}" \
			-t $(($(nproc) / 5 + 1)) -b10000 -n0 2>/dev/null &
		perf_pid=$!
	}
	perf_ipv6() {
		IP6=6 ${pktgen_script_path} -s100 \
			-i veth_a -d "${dst_addr6}" -p "${dst_port}" \
			-m "${dst_mac}" \
			-t $(($(nproc) / 5 + 1)) -b10000 -n0 2>/dev/null &
		perf_pid=$!
	}
}

# Clean up before each test
cleanup() {
	nft reset counter inet filter test	>/dev/null 2>&1
	nft flush ruleset			>/dev/null 2>&1
	ip link del dummy0			2>/dev/null
	ip route del default			2>/dev/null
	ip -6 route del default			2>/dev/null
	ip netns pids B				2>/dev/null | xargs kill 2>/dev/null
	ip netns del B				2>/dev/null
	ip link del veth_a			2>/dev/null
	timeout=
	killall iperf3				2>/dev/null
	killall iperf				2>/dev/null
	killall netperf				2>/dev/null
	killall netserver			2>/dev/null
}

cleanup_exit() {
	cleanup
	rm -f "$tmp"
}

# Entry point for setup functions
setup() {
	if [ "$(id -u)" -ne 0 ]; then
		echo "  need to run as root"
		exit ${ksft_skip}
	fi

	cleanup
	check_tools || return 1
	for arg do
		if ! eval setup_"${arg}"; then
			err "  ${arg} not supported"
			return 1
		fi
	done
}

# Format integer into IPv4 address, summing 10.0.0.5 (arbitrary) to it
format_addr4() {
	a=$((${1} + 16777216 * 10 + 5))
	printf "%i.%i.%i.%i"						\
	       "$((a / 16777216))" "$((a % 16777216 / 65536))"	\
	       "$((a % 65536 / 256))" "$((a % 256))"
}

# Format integer into IPv6 address, summing 2001:db8:: to it
format_addr6() {
	printf "2001:db8::%04x:%04x" "$((${1} / 65536))" "$((${1} % 65536))"
}

# Format integer into EUI-48 address, summing 00:01:00:00:00:00 to it
format_mac() {
	printf "00:01:%02x:%02x:%02x:%02x" \
	       "$((${1} / 16777216))" "$((${1} % 16777216 / 65536))"	\
	       "$((${1} % 65536 / 256))" "$((${1} % 256))"
}

# Format integer into port, avoid 0 port
format_port() {
	printf "%i" "$((${1} % 65534 + 1))"
}

# Drop suffixed '6' from L4 protocol, if any
format_proto() {
	printf "%s" "${proto}" | tr -d 6
}

# Format destination and source fields into nft concatenated type
format() {
	__start=
	__end=
	__expr="{ "

	for f in ${dst}; do
		[ "${__expr}" != "{ " ] && __expr="${__expr} . "

		__start="$(eval format_"${f}" "${start}")"
		__end="$(eval format_"${f}" "${end}")"

		if [ "${f}" = "proto" ]; then
			__expr="${__expr}${__start}"
		else
			__expr="${__expr}${__start}-${__end}"
		fi
	done
	for f in ${src}; do
		[ "${__expr}" != "{ " ] && __expr="${__expr} . "

		__start="$(eval format_"${f}" "${srcstart}")"
		__end="$(eval format_"${f}" "${srcend}")"

		if [ "${f}" = "proto" ]; then
			__expr="${__expr}${__start}"
		else
			__expr="${__expr}${__start}-${__end}"
		fi
	done

	if [ -n "${timeout}" ]; then
		echo "${__expr} timeout ${timeout}s }"
	else
		echo "${__expr} }"
	fi
}

# Format destination and source fields into nft type, start element only
format_norange() {
	__expr="{ "

	for f in ${dst}; do
		[ "${__expr}" != "{ " ] && __expr="${__expr} . "

		__expr="${__expr}$(eval format_"${f}" "${start}")"
	done
	for f in ${src}; do
		__expr="${__expr} . $(eval format_"${f}" "${start}")"
	done

	echo "${__expr} }"
}

# Format first destination field into nft type
format_noconcat() {
	for f in ${dst}; do
		__start="$(eval format_"${f}" "${start}")"
		__end="$(eval format_"${f}" "${end}")"

		if [ "${f}" = "proto" ]; then
			echo "{ ${__start} }"
		else
			echo "{ ${__start}-${__end} }"
		fi
		return
	done
}

# Add single entry to 'test' set in 'inet filter' table
add() {
	if ! nft add element inet filter test "${1}"; then
		err "Failed to add ${1} given ruleset:"
		err "$(nft -a list ruleset)"
		return 1
	fi
}

# Format and output entries for sets in 'netdev perf' table
add_perf() {
	if [ "${1}" = "test" ]; then
		echo "add element netdev perf test $(format)"
	elif [ "${1}" = "norange" ]; then
		echo "add element netdev perf norange $(format_norange)"
	elif [ "${1}" = "noconcat" ]; then
		echo "add element netdev perf noconcat $(format_noconcat)"
	fi
}

# Add single entry to 'norange' set in 'netdev perf' table
add_perf_norange() {
	if ! nft add element netdev perf norange "${1}"; then
		err "Failed to add ${1} given ruleset:"
		err "$(nft -a list ruleset)"
		return 1
	fi
}

# Add single entry to 'noconcat' set in 'netdev perf' table
add_perf_noconcat() {
	if ! nft add element netdev perf noconcat "${1}"; then
		err "Failed to add ${1} given ruleset:"
		err "$(nft -a list ruleset)"
		return 1
	fi
}

# Delete single entry from set
del() {
	if ! nft delete element inet filter test "${1}"; then
		err "Failed to delete ${1} given ruleset:"
		err "$(nft -a list ruleset)"
		return 1
	fi
}

# Return packet count from 'test' counter in 'inet filter' table
count_packets() {
	found=0
	for token in $(nft list counter inet filter test); do
		[ ${found} -eq 1 ] && echo "${token}" && return
		[ "${token}" = "packets" ] && found=1
	done
}

# Return packet count from 'test' counter in 'netdev perf' table
count_perf_packets() {
	found=0
	for token in $(nft list counter netdev perf test); do
		[ ${found} -eq 1 ] && echo "${token}" && return
		[ "${token}" = "packets" ] && found=1
	done
}

# Set MAC addresses, send traffic according to specifier
flood() {
	ip link set veth_a address "$(format_mac "${1}")"
	ip -n B link set veth_b address "$(format_mac "${2}")"

	for f in ${dst}; do
		eval dst_"$f"=\$\(format_\$f "${1}"\)
	done
	for f in ${src}; do
		eval src_"$f"=\$\(format_\$f "${2}"\)
	done
	eval flood_\$proto
}

# Set MAC addresses, start pktgen injection
perf() {
	dst_mac="$(format_mac "${1}")"
	ip link set veth_a address "${dst_mac}"

	for f in ${dst}; do
		eval dst_"$f"=\$\(format_\$f "${1}"\)
	done
	for f in ${src}; do
		eval src_"$f"=\$\(format_\$f "${2}"\)
	done
	eval perf_\$perf_proto
}

# Set MAC addresses, send single packet, check that it matches, reset counter
send_match() {
	ip link set veth_a address "$(format_mac "${1}")"
	ip -n B link set veth_b address "$(format_mac "${2}")"

	for f in ${dst}; do
		eval dst_"$f"=\$\(format_\$f "${1}"\)
	done
	for f in ${src}; do
		eval src_"$f"=\$\(format_\$f "${2}"\)
	done
	eval send_\$proto
	if [ "$(count_packets)" != "1" ]; then
		err "${proto} packet to:"
		err "  $(for f in ${dst}; do
			 eval format_\$f "${1}"; printf ' '; done)"
		err "from:"
		err "  $(for f in ${src}; do
			 eval format_\$f "${2}"; printf ' '; done)"
		err "should have matched ruleset:"
		err "$(nft -a list ruleset)"
		return 1
	fi
	nft reset counter inet filter test >/dev/null
}

# Set MAC addresses, send single packet, check that it doesn't match
send_nomatch() {
	ip link set veth_a address "$(format_mac "${1}")"
	ip -n B link set veth_b address "$(format_mac "${2}")"

	for f in ${dst}; do
		eval dst_"$f"=\$\(format_\$f "${1}"\)
	done
	for f in ${src}; do
		eval src_"$f"=\$\(format_\$f "${2}"\)
	done
	eval send_\$proto
	if [ "$(count_packets)" != "0" ]; then
		err "${proto} packet to:"
		err "  $(for f in ${dst}; do
			 eval format_\$f "${1}"; printf ' '; done)"
		err "from:"
		err "  $(for f in ${src}; do
			 eval format_\$f "${2}"; printf ' '; done)"
		err "should not have matched ruleset:"
		err "$(nft -a list ruleset)"
		return 1
	fi
}

# Correctness test template:
# - add ranged element, check that packets match it
# - check that packets outside range don't match it
# - remove some elements, check that packets don't match anymore
test_correctness() {
	setup veth send_"${proto}" set || return ${ksft_skip}

	range_size=1
	for i in $(seq "${start}" $((start + count))); do
		end=$((start + range_size))

		# Avoid negative or zero-sized port ranges
		if [ $((end / 65534)) -gt $((start / 65534)) ]; then
			start=${end}
			end=$((end + 1))
		fi
		srcstart=$((start + src_delta))
		srcend=$((end + src_delta))

		add "$(format)" || return 1
		for j in $(seq "$start" $((range_size / 2 + 1)) ${end}); do
			send_match "${j}" $((j + src_delta)) || return 1
		done
		send_nomatch $((end + 1)) $((end + 1 + src_delta)) || return 1

		# Delete elements now and then
		if [ $((i % 3)) -eq 0 ]; then
			del "$(format)" || return 1
			for j in $(seq "$start" \
				   $((range_size / 2 + 1)) ${end}); do
				send_nomatch "${j}" $((j + src_delta)) \
					|| return 1
			done
		fi

		range_size=$((range_size + 1))
		start=$((end + range_size))
	done
}

# Concurrency test template:
# - add all the elements
# - start a thread for each physical thread that:
#   - adds all the elements
#   - flushes the set
#   - adds all the elements
#   - flushes the entire ruleset
#   - adds the set back
#   - adds all the elements
#   - delete all the elements
test_concurrency() {
	proto=${flood_proto}
	tools=${flood_tools}
	chain_spec=${flood_spec}
	setup veth flood_"${proto}" set || return ${ksft_skip}

	range_size=1
	cstart=${start}
	flood_pids=
	for i in $(seq "$start" $((start + count))); do
		end=$((start + range_size))
		srcstart=$((start + src_delta))
		srcend=$((end + src_delta))

		add "$(format)" || return 1

		flood "${i}" $((i + src_delta)) & flood_pids="${flood_pids} $!"

		range_size=$((range_size + 1))
		start=$((end + range_size))
	done

	sleep $((RANDOM%10))

	pids=
	for c in $(seq 1 "$(nproc)"); do (
		for r in $(seq 1 "${race_repeat}"); do
			range_size=1

			# $start needs to be local to this subshell
			# shellcheck disable=SC2030
			start=${cstart}
			for i in $(seq "$start" $((start + count))); do
				end=$((start + range_size))
				srcstart=$((start + src_delta))
				srcend=$((end + src_delta))

				add "$(format)" 2>/dev/null

				range_size=$((range_size + 1))
				start=$((end + range_size))
			done

			nft flush inet filter test 2>/dev/null

			range_size=1
			start=${cstart}
			for i in $(seq "$start" $((start + count))); do
				end=$((start + range_size))
				srcstart=$((start + src_delta))
				srcend=$((end + src_delta))

				add "$(format)" 2>/dev/null

				range_size=$((range_size + 1))
				start=$((end + range_size))
			done

			nft flush ruleset
			setup set 2>/dev/null

			range_size=1
			start=${cstart}
			for i in $(seq "$start" $((start + count))); do
				end=$((start + range_size))
				srcstart=$((start + src_delta))
				srcend=$((end + src_delta))

				add "$(format)" 2>/dev/null

				range_size=$((range_size + 1))
				start=$((end + range_size))
			done

			range_size=1
			start=${cstart}
			for i in $(seq "$start" $((start + count))); do
				end=$((start + range_size))
				srcstart=$((start + src_delta))
				srcend=$((end + src_delta))

				del "$(format)" 2>/dev/null

				range_size=$((range_size + 1))
				start=$((end + range_size))
			done
		done
	) & pids="${pids} $!"
	done

	# shellcheck disable=SC2046,SC2086 # word splitting wanted here
	wait $(for pid in ${pids}; do echo ${pid}; done)
	# shellcheck disable=SC2046,SC2086
	kill $(for pid in ${flood_pids}; do echo ${pid}; done) 2>/dev/null
	# shellcheck disable=SC2046,SC2086
	wait $(for pid in ${flood_pids}; do echo ${pid}; done) 2>/dev/null

	return 0
}

# Timeout test template:
# - add all the elements with 3s timeout while checking that packets match
# - wait 3s after the last insertion, check that packets don't match any entry
test_timeout() {
	setup veth send_"${proto}" set || return ${ksft_skip}

	timeout=3

	[ "$KSFT_MACHINE_SLOW" = "yes" ] && timeout=8

	range_size=1
	for i in $(seq "$start" $((start + count))); do
		end=$((start + range_size))
		srcstart=$((start + src_delta))
		srcend=$((end + src_delta))

		add "$(format)" || return 1

		for j in $(seq "$start" $((range_size / 2 + 1)) ${end}); do
			send_match "${j}" $((j + src_delta)) || return 1
		done

		range_size=$((range_size + 1))
		start=$((end + range_size))
	done
	sleep $timeout
	for i in $(seq "$start" $((start + count))); do
		end=$((start + range_size))
		srcstart=$((start + src_delta))
		srcend=$((end + src_delta))

		for j in $(seq "$start" $((range_size / 2 + 1)) ${end}); do
			send_nomatch "${j}" $((j + src_delta)) || return 1
		done

		range_size=$((range_size + 1))
		start=$((end + range_size))
	done
}

# Performance test template:
# - add concatenated ranged entries
# - add non-ranged concatenated entries (for hash set matching rate baseline)
# - add ranged entries with first field only (for rbhash baseline)
# - start pktgen injection directly on device rx path of this namespace
# - measure drop only rate, hash and rbtree baselines, then matching rate
test_performance() {
	chain_spec=${perf_spec}
	dst="${perf_dst}"
	src="${perf_src}"
	setup veth perf set || return ${ksft_skip}

	first=${start}
	range_size=1
	for set in test norange noconcat; do
		start=${first}
		for i in $(seq "$start" $((start + perf_entries))); do
			end=$((start + range_size))
			srcstart=$((start + src_delta))
			srcend=$((end + src_delta))

			if [ $((end / 65534)) -gt $((start / 65534)) ]; then
				start=${end}
				end=$((end + 1))
			elif [ "$start" -eq "$end" ]; then
				end=$((start + 1))
			fi

			add_perf ${set}

			start=$((end + range_size))
		done > "${tmp}"
		nft -f "${tmp}"
	done

	perf $((end - 1)) "$srcstart"

	sleep 2

	nft add rule netdev perf test counter name \"test\" drop
	nft reset counter netdev perf test >/dev/null 2>&1
	sleep "${perf_duration}"
	pps="$(printf %10s $(($(count_perf_packets) / perf_duration)))"
	info "    baseline (drop from netdev hook):            ${pps}pps"
	handle="$(nft -a list chain netdev perf test | grep counter)"
	handle="${handle##* }"
	nft delete rule netdev perf test handle "${handle}"

	nft add rule "netdev perf test ${chain_spec} @norange \
		counter name \"test\" drop"
	nft reset counter netdev perf test >/dev/null 2>&1
	sleep "${perf_duration}"
	pps="$(printf %10s $(($(count_perf_packets) / perf_duration)))"
	info "    baseline hash (non-ranged entries):          ${pps}pps"
	handle="$(nft -a list chain netdev perf test | grep counter)"
	handle="${handle##* }"
	nft delete rule netdev perf test handle "${handle}"

	nft add rule "netdev perf test ${chain_spec%%. *} @noconcat \
		counter name \"test\" drop"
	nft reset counter netdev perf test >/dev/null 2>&1
	sleep "${perf_duration}"
	pps="$(printf %10s $(($(count_perf_packets) / perf_duration)))"
	info "    baseline rbtree (match on first field only): ${pps}pps"
	handle="$(nft -a list chain netdev perf test | grep counter)"
	handle="${handle##* }"
	nft delete rule netdev perf test handle "${handle}"

	nft add rule "netdev perf test ${chain_spec} @test \
		counter name \"test\" drop"
	nft reset counter netdev perf test >/dev/null 2>&1
	sleep "${perf_duration}"
	pps="$(printf %10s $(($(count_perf_packets) / perf_duration)))"
	p5="$(printf %5s "${perf_entries}")"
	info "    set with ${p5} full, ranged entries:         ${pps}pps"
	kill "${perf_pid}"
}

test_bug_flush_remove_add() {
	rounds=100
	[ "$KSFT_MACHINE_SLOW" = "yes" ] && rounds=10

	set_cmd='{ set s { type ipv4_addr . inet_service; flags interval; }; }'
	elem1='{ 10.0.0.1 . 22-25, 10.0.0.1 . 10-20 }'
	elem2='{ 10.0.0.1 . 10-20, 10.0.0.1 . 22-25 }'
	for i in $(seq 1 $rounds); do
		nft add table t "$set_cmd"	|| return ${ksft_skip}
		nft add element t s "$elem1"	2>/dev/null || return 1
		nft flush set t s		2>/dev/null || return 1
		nft add element t s "$elem2"	2>/dev/null || return 1
	done
	nft flush ruleset
}

# - add ranged element, check that packets match it
# - reload the set, check packets still match
test_bug_reload() {
	setup veth send_"${proto}" set || return ${ksft_skip}
	rstart=${start}

	range_size=1
	for i in $(seq "${start}" $((start + count))); do
		end=$((start + range_size))

		# Avoid negative or zero-sized port ranges
		if [ $((end / 65534)) -gt $((start / 65534)) ]; then
			start=${end}
			end=$((end + 1))
		fi
		srcstart=$((start + src_delta))
		srcend=$((end + src_delta))

		add "$(format)" || return 1
		range_size=$((range_size + 1))
		start=$((end + range_size))
	done

	# check kernel does allocate pcpu sctrach map
	# for reload with no elemet add/delete
	( echo flush set inet filter test ;
	  nft list set inet filter test ) | nft -f -

	start=${rstart}
	range_size=1

	for i in $(seq "${start}" $((start + count))); do
		end=$((start + range_size))

		# Avoid negative or zero-sized port ranges
		if [ $((end / 65534)) -gt $((start / 65534)) ]; then
			start=${end}
			end=$((end + 1))
		fi
		srcstart=$((start + src_delta))
		srcend=$((end + src_delta))

		for j in $(seq "$start" $((range_size / 2 + 1)) ${end}); do
			send_match "${j}" $((j + src_delta)) || return 1
		done

		range_size=$((range_size + 1))
		start=$((end + range_size))
	done

	nft flush ruleset
}

test_reported_issues() {
	eval test_bug_"${subtest}"
}

# Run everything in a separate network namespace
[ "${1}" != "run" ] && { unshare -n "${0}" run; exit $?; }
tmp="$(mktemp)"
trap cleanup_exit EXIT

# Entry point for test runs
passed=0
for name in ${TESTS}; do
	printf "TEST: %s\n" "$(echo "$name" | tr '_' ' ')"
	if [ "${name}" = "reported_issues" ]; then
		SUBTESTS="${BUGS}"
	else
		SUBTESTS="${TYPES}"
	fi

	for subtest in ${SUBTESTS}; do
		eval desc=\$TYPE_"${subtest}"
		IFS='
'
		for __line in ${desc}; do
			# shellcheck disable=SC2086
			eval ${__line%%	*}=\"${__line##*	}\";
		done
		IFS=' 	
'

		if [ "${name}" = "concurrency" ] && \
		   [ "${race_repeat}" = "0" ]; then
			continue
		fi
		if [ "${name}" = "performance" ] && \
		   [ "${perf_duration}" = "0" ]; then
			continue
		fi

		[ "$KSFT_MACHINE_SLOW" = "yes" ] && count=1

		printf "  %-32s  " "${display}"
		tthen=$(date +%s)
		eval test_"${name}"
		ret=$?

		tnow=$(date +%s)
		printf "%5ds%-30s" $((tnow-tthen))

		if [ $ret -eq 0 ]; then
			printf "[ OK ]\n"
			info_flush
			passed=$((passed + 1))
		elif [ $ret -eq 1 ]; then
			printf "[FAIL]\n"
			err_flush
			exit 1
		elif [ $ret -eq ${ksft_skip} ]; then
			printf "[SKIP]\n"
			err_flush
		fi
	done
done

[ ${passed} -eq 0 ] && exit ${ksft_skip} || exit 0
