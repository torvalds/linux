#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# OVS kernel module self tests

trap ovs_exit_sig EXIT TERM INT ERR

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

PAUSE_ON_FAIL=no
VERBOSE=0
TRACING=0
WAIT_TIMEOUT=5

if test "X$KSFT_MACHINE_SLOW" == "Xyes"; then
	WAIT_TIMEOUT=10
fi

tests="
	arp_ping				eth-arp: Basic arp ping between two NS
	ct_connect_v4				ip4-ct-xon: Basic ipv4 tcp connection using ct
	connect_v4				ip4-xon: Basic ipv4 ping between two NS
	nat_connect_v4				ip4-nat-xon: Basic ipv4 tcp connection via NAT
	nat_related_v4				ip4-nat-related: ICMP related matches work with SNAT
	netlink_checks				ovsnl: validate netlink attrs and settings
	upcall_interfaces			ovs: test the upcall interfaces
	tunnel_metadata				ovs: test extraction of tunnel metadata
	tunnel_refcount				ovs: test tunnel vport reference cleanup
	drop_reason				drop: test drop reasons are emitted
	pop_vlan				vlan: POP_VLAN action strips tag
	dec_ttl					ttl: dec_ttl decrements IP TTL
	flow_set				flow-set: Flow modify
	action_set				set: SET action rewrites fields
	psample					psample: Sampling packets with psample"

info() {
	[ "${ovs_dir}" != "" ] &&
		echo "`date +"[%m-%d %H:%M:%S]"` $*" >> ${ovs_dir}/debug.log
	[ $VERBOSE = 0 ] || echo $*
}

ovs_wait() {
	info "waiting $WAIT_TIMEOUT s for: $@"

	if "$@" ; then
		info "wait succeeded immediately"
		return 0
	fi

	# A quick re-check helps speed up small races in fast systems.
	# However, fractional sleeps might not necessarily work.
	local start=0
	sleep 0.1 || { sleep 1; start=1; }

	for (( i=start; i<WAIT_TIMEOUT; i++ )); do
		if "$@" ; then
			info "wait succeeded after $i seconds"
			return 0
		fi
		sleep 1
	done
	info "wait failed after $i seconds"
	return 1
}

ovs_base=`pwd`
sbxs=
sbx_add () {
	info "adding sandbox '$1'"

	sbxs="$sbxs $1"

	NO_BIN=0

	# Create sandbox.
	local d="$ovs_base"/$1
	if [ -e $d ]; then
		info "removing $d"
		rm -rf "$d"
	fi
	mkdir "$d" || return 1
	ovs_setenv $1
}

ovs_exit_sig() {
	[ -e ${ovs_dir}/cleanup ] && . "$ovs_dir/cleanup"
}

on_exit() {
	echo "$1" > ${ovs_dir}/cleanup.tmp
	cat ${ovs_dir}/cleanup >> ${ovs_dir}/cleanup.tmp
	mv ${ovs_dir}/cleanup.tmp ${ovs_dir}/cleanup
}

ovs_setenv() {
	sandbox=$1

	ovs_dir=$ovs_base${1:+/$1}; export ovs_dir

	test -e ${ovs_dir}/cleanup || : > ${ovs_dir}/cleanup
}

ovs_sbx() {
	if test "X$2" != X; then
		(ovs_setenv $1; shift;
		 info "run cmd: $@"; "$@" >> ${ovs_dir}/debug.log)
	else
		ovs_setenv $1
	fi
}

ovs_add_dp () {
	info "Adding DP/Bridge IF: sbx:$1 dp:$2 {$3, $4, $5}"
	sbxname="$1"
	shift
	ovs_sbx "$sbxname" python3 $ovs_base/ovs-dpctl.py add-dp $*
	on_exit "ovs_sbx $sbxname python3 $ovs_base/ovs-dpctl.py del-dp $1;"
}

ovs_add_if () {
	info "Adding IF to DP: br:$3 if:$4 ($2)"
	if [ "$5" != "-u" ]; then
		ovs_sbx "$1" python3 $ovs_base/ovs-dpctl.py add-if \
		    -t "$2" "$3" "$4" || return 1
	else
		python3 $ovs_base/ovs-dpctl.py add-if \
		    -u -t "$2" "$3" "$4" >$ovs_dir/$4.out 2>$ovs_dir/$4.err &
		pid=$!
		on_exit "ovs_sbx $1 kill -TERM $pid 2>/dev/null"
	fi
}

ovs_del_if () {
	info "Deleting IF from DP: br:$2 if:$3"
	ovs_sbx "$1" python3 $ovs_base/ovs-dpctl.py del-if "$2" "$3" || return 1
}

ovs_netns_spawn_daemon() {
	sbx=$1
	shift
	netns=$1
	shift
	if [ "$netns" == "_default" ]; then
		$*  >> $ovs_dir/stdout  2>> $ovs_dir/stderr &
	else
		ip netns exec $netns $*  >> $ovs_dir/stdout  2>> $ovs_dir/stderr &
	fi
	pid=$!
	ovs_sbx "$sbx" on_exit "kill -TERM $pid 2>/dev/null"
}

ovs_spawn_daemon() {
	sbx=$1
	shift
	ovs_netns_spawn_daemon $sbx "_default" $*
}

ovs_add_netns_and_veths () {
	info "Adding netns attached: sbx:$1 dp:$2 {$3, $4, $5}"
	ovs_sbx "$1" ip netns add "$3" || return 1
	on_exit "ovs_sbx $1 ip netns del $3"
	ovs_sbx "$1" ip link add "$4" type veth peer name "$5" || return 1
	on_exit "ovs_sbx $1 ip link del $4 >/dev/null 2>&1"
	ovs_sbx "$1" ip link set "$4" up || return 1
	ovs_sbx "$1" ip link set "$5" netns "$3" || return 1
	ovs_sbx "$1" ip netns exec "$3" ip link set "$5" up || return 1

	if [ "$6" != "" ]; then
		ovs_sbx "$1" ip netns exec "$3" ip addr add "$6" dev "$5" \
		    || return 1
	fi

	if [ "$7" != "-u" ]; then
		ovs_add_if "$1" "netdev" "$2" "$4" || return 1
	else
		ovs_add_if "$1" "netdev" "$2" "$4" -u || return 1
	fi

	if [ $TRACING -eq 1 ]; then
		ovs_netns_spawn_daemon "$1" "$3" tcpdump -l -i any -s 6553
		ovs_wait grep -q "listening on any" ${ovs_dir}/stderr
	fi

	return 0
}

ovs_add_flow () {
	info "Adding flow to DP: sbx:$1 br:$2 flow:$3 act:$4"
	ovs_sbx "$1" python3 $ovs_base/ovs-dpctl.py add-flow "$2" "$3" "$4"
	if [ $? -ne 0 ]; then
		info "Flow [ $3 : $4 ] failed"
		return 1
	fi
	return 0
}

ovs_mod_flow () {
	if [ -n "$4" ]; then
		info "Modifying flow: sbx:$1 br:$2 flow:$3 act:$4"
		ovs_sbx "$1" python3 $ovs_base/ovs-dpctl.py \
			mod-flow "$2" "$3" "$4"
	else
		info "Modifying flow (no actions): sbx:$1 br:$2 flow:$3"
		ovs_sbx "$1" python3 $ovs_base/ovs-dpctl.py \
			mod-flow "$2" "$3"
	fi
	if [ $? -ne 0 ]; then
		info "Flow modify [ $3 ] failed"
		return 1
	fi
	return 0
}

ovs_del_flows () {
	info "Deleting all flows from DP: sbx:$1 br:$2"
	ovs_sbx "$1" python3 $ovs_base/ovs-dpctl.py del-flows "$2"
	return 0
}

ovs_drop_record_and_run () {
	local sbx=$1
	shift

	perf record -a -q -e skb:kfree_skb -o ${ovs_dir}/perf.data $* \
		>> ${ovs_dir}/stdout 2>> ${ovs_dir}/stderr
	return $?
}

ovs_drop_reason_count()
{
	local reason=$1

	local perf_output=`perf script -i ${ovs_dir}/perf.data -F trace:event,trace`
	local pattern="skb:kfree_skb:.*reason: $reason"

	return `echo "$perf_output" | grep "$pattern" | wc -l`
}

ovs_test_flow_fails () {
	ERR_MSG="Flow actions may not be safe on all matching packets"

	PRE_TEST=$(dmesg | grep -c "${ERR_MSG}")
	ovs_add_flow $@ &> /dev/null $@ && return 1
	POST_TEST=$(dmesg | grep -c "${ERR_MSG}")

	if [ "$PRE_TEST" == "$POST_TEST" ]; then
		return 1
	fi
	return 0
}

usage() {
	echo
	echo "$0 [OPTIONS] [TEST]..."
	echo "If no TEST argument is given, all tests will be run."
	echo
	echo "Options"
	echo "  -t: capture traffic via tcpdump"
	echo "  -v: verbose"
	echo "  -p: pause on failure"
	echo
	echo "Available tests${tests}"
	exit 1
}


test_dec_ttl() {
	sbx_add "test_dec_ttl" || return $?
	ovs_add_dp "test_dec_ttl" decttl || return 1

	info "create namespaces"
	for ns in client server; do
		ovs_add_netns_and_veths "test_dec_ttl" "decttl" "$ns" \
			"${ns:0:1}0" "${ns:0:1}1" || return 1
	done

	ip netns exec client ip addr add 10.0.0.1/24 dev c1
	ip netns exec client ip link set c1 up
	ip netns exec server ip addr add 10.0.0.2/24 dev s1
	ip netns exec server ip link set s1 up

	# Probe: check if kernel supports dec_ttl action.
	ovs_add_flow "test_dec_ttl" decttl \
		'in_port(1),eth(),eth_type(0x0800),ipv4()' \
		'dec_ttl(le_1())' &>/dev/null
	if [ $? -ne 0 ]; then
		info "no support for dec_ttl - skipping"
		ovs_exit_sig
		return $ksft_skip
	fi

	ovs_del_flows "test_dec_ttl" decttl

	# ARP flows (bidirectional)
	ovs_add_flow "test_dec_ttl" decttl \
		'in_port(1),eth(),eth_type(0x0806),arp()' '2' || return 1
	ovs_add_flow "test_dec_ttl" decttl \
		'in_port(2),eth(),eth_type(0x0806),arp()' '1' || return 1

	# IP flows with dec_ttl action
	ovs_add_flow "test_dec_ttl" decttl \
		'in_port(1),eth(),eth_type(0x0800),ipv4()' \
		'dec_ttl(le_1()),2' || return 1
	ovs_add_flow "test_dec_ttl" decttl \
		'in_port(2),eth(),eth_type(0x0800),ipv4()' \
		'dec_ttl(le_1()),1' || return 1

	info "verify connectivity with dec_ttl"
	ovs_sbx "test_dec_ttl" ip netns exec client ping -c 1 -W 2 \
		10.0.0.2 || return 1

	info "verify TTL=1 is dropped by dec_ttl"
	ovs_sbx "test_dec_ttl" ip netns exec client ping -c 1 -W 2 \
		-t 1 10.0.0.2 >/dev/null 2>&1 \
		&& { info "FAIL: ping should fail with TTL=1 and dec_ttl"
		     return 1; }

	return 0
}

test_flow_set() {
	sbx_add "test_flow_set" || return $?
	ovs_add_dp "test_flow_set" flowset || return 1

	info "create namespaces"
	for ns in client server; do
		ovs_add_netns_and_veths "test_flow_set" "flowset" "$ns" \
			"${ns:0:1}0" "${ns:0:1}1" || return 1
	done

	ip netns exec client ip addr add 10.0.0.1/24 dev c1
	ip netns exec client ip link set c1 up
	ip netns exec server ip addr add 10.0.0.2/24 dev s1
	ip netns exec server ip link set s1 up

	ovs_add_flow "test_flow_set" flowset \
		'in_port(1),eth(),eth_type(0x0806),arp()' '2' || return 1
	ovs_add_flow "test_flow_set" flowset \
		'in_port(2),eth(),eth_type(0x0806),arp()' '1' || return 1

	local fwd_flow="ufid:00000001-0002-0003-0004-000500060007"
	fwd_flow="$fwd_flow,in_port(1),eth(),eth_type(0x0800),ipv4()"

	ovs_add_flow "test_flow_set" flowset "$fwd_flow" '2' \
		|| return 1
	ovs_add_flow "test_flow_set" flowset \
		'in_port(2),eth(),eth_type(0x0800),ipv4()' '1' || return 1

	info "verify initial forwarding"
	ovs_sbx "test_flow_set" ip netns exec client ping -c 1 -W 2 \
		10.0.0.2 || return 1

	info "mod-flow with new actions (change to drop)"
	ovs_mod_flow "test_flow_set" flowset "$fwd_flow" 'drop' \
		|| return 1

	info "verify traffic is now dropped"
	ovs_sbx "test_flow_set" ip netns exec client ping -c 1 -W 2 \
		10.0.0.2 >/dev/null 2>&1 \
		&& { info "FAIL: ping should fail after mod-flow to drop"
		     return 1; }

	info "mod-flow without actions"
	ovs_mod_flow "test_flow_set" flowset "$fwd_flow" || return 1

	info "verify flow retained drop action via dump"
	python3 "$ovs_base/ovs-dpctl.py" dump-flows flowset \
		| grep -q "actions:drop" || \
		{ info "FAIL: flow not showing drop action"; return 1; }

	info "verify drop actions unchanged"
	ovs_sbx "test_flow_set" ip netns exec client ping -c 1 -W 2 \
		10.0.0.2 >/dev/null 2>&1 \
		&& { info "FAIL: ping should still fail after no-actions set"
		     return 1; }

	return 0
}

test_action_set() {
	sbx_add "test_action_set" || return $?
	ovs_add_dp "test_action_set" settest || return 1

	info "create namespaces"
	for ns in client server; do
		ovs_add_netns_and_veths "test_action_set" "settest" "$ns" \
			"${ns:0:1}0" "${ns:0:1}1" || return 1
	done

	ip netns exec client ip addr add 10.0.0.1/24 dev c1
	ip netns exec client ip link set c1 up
	ip netns exec server ip addr add 10.0.0.2/24 dev s1
	ip netns exec server ip link set s1 up

	ovs_add_flow "test_action_set" settest \
		'in_port(1),eth(),eth_type(0x0806),arp()' '2' || return 1
	ovs_add_flow "test_action_set" settest \
		'in_port(2),eth(),eth_type(0x0806),arp()' '1' || return 1

	ovs_add_flow "test_action_set" settest \
		'in_port(1),eth(),eth_type(0x0800),ipv4()' '2' || return 1
	ovs_add_flow "test_action_set" settest \
		'in_port(2),eth(),eth_type(0x0800),ipv4()' '1' || return 1

	info "verify connectivity without SET"
	ovs_sbx "test_action_set" ip netns exec client ping -c 1 -W 2 \
		10.0.0.2 || return 1

	ovs_del_flows "test_action_set" settest
	ovs_add_flow "test_action_set" settest \
		'in_port(1),eth(),eth_type(0x0806),arp()' '2' || return 1
	ovs_add_flow "test_action_set" settest \
		'in_port(2),eth(),eth_type(0x0806),arp()' '1' || return 1

	info "set ipv4 dst to unreachable address"
	ovs_add_flow "test_action_set" settest \
		'in_port(1),eth(),eth_type(0x0800),ipv4()' \
		'set(ipv4(dst=10.0.0.99)),2' || return 1
	ovs_add_flow "test_action_set" settest \
		'in_port(2),eth(),eth_type(0x0800),ipv4()' '1' || return 1

	info "verify ping fails with rewritten dst"
	ovs_sbx "test_action_set" ip netns exec client ping -c 1 -W 2 \
		10.0.0.2 >/dev/null 2>&1 \
		&& { info "FAIL: ping should fail with dst rewritten"
		     return 1; }

	ovs_del_flows "test_action_set" settest
	ovs_add_flow "test_action_set" settest \
		'in_port(1),eth(),eth_type(0x0806),arp()' '2' || return 1
	ovs_add_flow "test_action_set" settest \
		'in_port(2),eth(),eth_type(0x0806),arp()' '1' || return 1
	ovs_add_flow "test_action_set" settest \
		'in_port(1),eth(),eth_type(0x0800),ipv4()' '2' || return 1
	ovs_add_flow "test_action_set" settest \
		'in_port(2),eth(),eth_type(0x0800),ipv4()' '1' || return 1

	info "verify connectivity restored without SET"
	ovs_sbx "test_action_set" ip netns exec client ping -c 1 -W 2 \
		10.0.0.2 || return 1

	return 0
}

# psample test
# - use psample to observe packets
test_psample() {
	sbx_add "test_psample" || return $?

	# Add a datapath with per-vport dispatching.
	ovs_add_dp "test_psample" psample -V 2:1 || return 1

	info "create namespaces"
	ovs_add_netns_and_veths "test_psample" "psample" \
		client c0 c1 172.31.110.10/24 -u || return 1
	ovs_add_netns_and_veths "test_psample" "psample" \
		server s0 s1 172.31.110.20/24 -u || return 1

	# Check if psample actions can be configured.
	ovs_add_flow "test_psample" psample \
	'in_port(1),eth(),eth_type(0x0806),arp()' 'psample(group=1)' &> /dev/null
	if [ $? == 1 ]; then
		info "no support for psample - skipping"
		ovs_exit_sig
		return $ksft_skip
	fi

	ovs_del_flows "test_psample" psample

	# Test action verification.
	OLDIFS=$IFS
	IFS='*'
	min_key='in_port(1),eth(),eth_type(0x0800),ipv4()'
	for testcase in \
		"cookie to large"*"psample(group=1,cookie=1615141312111009080706050403020100)" \
		"no group with cookie"*"psample(cookie=abcd)" \
		"no group"*"psample()";
	do
		set -- $testcase;
		ovs_test_flow_fails "test_psample" psample $min_key $2
		if [ $? == 1 ]; then
			info "failed - $1"
			return 1
		fi
	done
	IFS=$OLDIFS

	ovs_del_flows "test_psample" psample
	# Allow ARP
	ovs_add_flow "test_psample" psample \
		'in_port(1),eth(),eth_type(0x0806),arp()' '2' || return 1
	ovs_add_flow "test_psample" psample \
		'in_port(2),eth(),eth_type(0x0806),arp()' '1' || return 1

	# Sample first 14 bytes of all traffic.
	ovs_add_flow "test_psample" psample \
	    "in_port(1),eth(),eth_type(0x0800),ipv4()" \
            "trunc(14),psample(group=1,cookie=c0ffee),2"

	# Sample all traffic. In this case, use a sample() action with both
	# psample and an upcall emulating simultaneous local sampling and
	# sFlow / IPFIX.
	nlpid=$(grep -E "listening on upcall packet handler" \
            $ovs_dir/s0.out | cut -d ":" -f 2 | tr -d ' ')
	[ -z "$nlpid" ] && \
		{ info "failed to get upcall PID"; return 1; }

	ovs_add_flow "test_psample" psample \
            "in_port(2),eth(),eth_type(0x0800),ipv4()" \
            "sample(sample=100%,actions(psample(group=2,cookie=eeff0c),userspace(pid=${nlpid},userdata=eeff0c))),1"

	# Record psample data.
	ovs_spawn_daemon "test_psample" python3 $ovs_base/ovs-dpctl.py psample-events
	ovs_wait grep -q "listening for psample events" ${ovs_dir}/stdout

	# Send a single ping.
	ovs_sbx "test_psample" ip netns exec client ping -I c1 172.31.110.20 -c 1 || return 1

	# We should have received one userspace action upcall and 2 psample packets.
	ovs_wait grep -q "userspace action command" $ovs_dir/s0.out || return 1

	# client -> server samples should only contain the first 14 bytes of the packet.
	ovs_wait grep -qE "rate:4294967295,group:1,cookie:c0ffee data:[0-9a-f]{28}$" \
		$ovs_dir/stdout || return 1

	ovs_wait grep -q "rate:4294967295,group:2,cookie:eeff0c" $ovs_dir/stdout || return 1

	return 0
}

# drop_reason test
# - drop packets and verify the right drop reason is reported
test_drop_reason() {
	which perf >/dev/null 2>&1 || return $ksft_skip
	which pahole >/dev/null 2>&1 || return $ksft_skip

	ovs_drop_subsys=$(pahole -C skb_drop_reason_subsys |
			      awk '/OPENVSWITCH/ { print $3; }' |
			      tr -d ,)
	if [ -z "$ovs_drop_subsys" ]; then
		info "failed to get OVS drop subsys ID"
		return $ksft_skip
	fi

	sbx_add "test_drop_reason" || return $?

	ovs_add_dp "test_drop_reason" dropreason || return 1

	info "create namespaces"
	for ns in client server; do
		ovs_add_netns_and_veths "test_drop_reason" "dropreason" "$ns" \
			"${ns:0:1}0" "${ns:0:1}1" || return 1
	done

	# Setup client namespace
	ip netns exec client ip addr add 172.31.110.10/24 dev c1
	ip netns exec client ip link set c1 up

	# Setup server namespace
	ip netns exec server ip addr add 172.31.110.20/24 dev s1
	ip netns exec server ip link set s1 up

	# Check if drop reasons can be sent
	ovs_add_flow "test_drop_reason" dropreason \
		'in_port(1),eth(),eth_type(0x0806),arp()' 'drop(10)' 2>/dev/null
	if [ $? == 1 ]; then
		info "no support for drop reasons - skipping"
		ovs_exit_sig
		return $ksft_skip
	fi

	ovs_del_flows "test_drop_reason" dropreason

	# Allow ARP
	ovs_add_flow "test_drop_reason" dropreason \
		'in_port(1),eth(),eth_type(0x0806),arp()' '2' || return 1
	ovs_add_flow "test_drop_reason" dropreason \
		'in_port(2),eth(),eth_type(0x0806),arp()' '1' || return 1

	# Allow client ICMP traffic but drop return path
	ovs_add_flow "test_drop_reason" dropreason \
		"in_port(1),eth(),eth_type(0x0800),ipv4(src=172.31.110.10,proto=1),icmp()" '2'
	ovs_add_flow "test_drop_reason" dropreason \
		"in_port(2),eth(),eth_type(0x0800),ipv4(src=172.31.110.20,proto=1),icmp()" 'drop'

	ovs_drop_record_and_run "test_drop_reason" ip netns exec client ping -c 2 172.31.110.20
	ovs_drop_reason_count 0x${ovs_drop_subsys}0001 # OVS_DROP_FLOW_ACTION
	if [[ "$?" -ne "2" ]]; then
		info "Did not detect expected drops: $?"
		return 1
	fi

	# Drop UDP 6000 traffic with an explicit action and an error code.
	ovs_add_flow "test_drop_reason" dropreason \
		"in_port(1),eth(),eth_type(0x0800),ipv4(src=172.31.110.10,proto=17),udp(dst=6000)" \
                'drop(42)'
	# Drop UDP 7000 traffic with an explicit action with no error code.
	ovs_add_flow "test_drop_reason" dropreason \
		"in_port(1),eth(),eth_type(0x0800),ipv4(src=172.31.110.10,proto=17),udp(dst=7000)" \
                'drop(0)'

	ovs_drop_record_and_run \
            "test_drop_reason" ip netns exec client nc -i 1 -zuv 172.31.110.20 6000
	ovs_drop_reason_count 0x${ovs_drop_subsys}0004 # OVS_DROP_EXPLICIT_ACTION_ERROR
	if [[ "$?" -ne "1" ]]; then
		info "Did not detect expected explicit error drops: $?"
		return 1
	fi

	ovs_drop_record_and_run \
            "test_drop_reason" ip netns exec client nc -i 1 -zuv 172.31.110.20 7000
	ovs_drop_reason_count 0x${ovs_drop_subsys}0003 # OVS_DROP_EXPLICIT_ACTION
	if [[ "$?" -ne "1" ]]; then
		info "Did not detect expected explicit drops: $?"
		return 1
	fi

	return 0
}

# arp_ping test
# - client has 1500 byte MTU
# - server has 1500 byte MTU
# - send ARP ping between two ns
test_arp_ping () {

	which arping >/dev/null 2>&1 || return $ksft_skip

	sbx_add "test_arp_ping" || return $?

	ovs_add_dp "test_arp_ping" arpping || return 1

	info "create namespaces"
	for ns in client server; do
		ovs_add_netns_and_veths "test_arp_ping" "arpping" "$ns" \
		    "${ns:0:1}0" "${ns:0:1}1" || return 1
	done

	# Setup client namespace
	ip netns exec client ip addr add 172.31.110.10/24 dev c1
	ip netns exec client ip link set c1 up
	HW_CLIENT=$(ip netns exec client ip link show dev c1 \
		| awk '/link\/ether/ {print $2}')
	[ -z "$HW_CLIENT" ] && \
		{ info "failed to get client hwaddr"; return 1; }
	info "Client hwaddr: $HW_CLIENT"

	# Setup server namespace
	ip netns exec server ip addr add 172.31.110.20/24 dev s1
	ip netns exec server ip link set s1 up
	HW_SERVER=$(ip netns exec server ip link show dev s1 \
		| awk '/link\/ether/ {print $2}')
	[ -z "$HW_SERVER" ] && \
		{ info "failed to get server hwaddr"; return 1; }
	info "Server hwaddr: $HW_SERVER"

	ovs_add_flow "test_arp_ping" arpping \
		"in_port(1),eth(),eth_type(0x0806),arp(sip=172.31.110.10,tip=172.31.110.20,sha=$HW_CLIENT,tha=ff:ff:ff:ff:ff:ff)" '2' || return 1
	ovs_add_flow "test_arp_ping" arpping \
		"in_port(2),eth(),eth_type(0x0806),arp()" '1' || return 1

	ovs_sbx "test_arp_ping" ip netns exec client arping -I c1 172.31.110.20 -c 1 || return 1

	return 0
}

# ct_connect_v4 test
#  - client has 1500 byte MTU
#  - server has 1500 byte MTU
#  - use ICMP to ping in each direction
#  - only allow CT state stuff to pass through new in c -> s
test_ct_connect_v4 () {

	which nc >/dev/null 2>/dev/null || return $ksft_skip

	sbx_add "test_ct_connect_v4" || return $?

	ovs_add_dp "test_ct_connect_v4" ct4 || return 1
	info "create namespaces"
	for ns in client server; do
		ovs_add_netns_and_veths "test_ct_connect_v4" "ct4" "$ns" \
		    "${ns:0:1}0" "${ns:0:1}1" || return 1
	done

	ip netns exec client ip addr add 172.31.110.10/24 dev c1
	ip netns exec client ip link set c1 up
	ip netns exec server ip addr add 172.31.110.20/24 dev s1
	ip netns exec server ip link set s1 up

	# Add forwarding for ARP and ip packets - completely wildcarded
	ovs_add_flow "test_ct_connect_v4" ct4 \
		'in_port(1),eth(),eth_type(0x0806),arp()' '2' || return 1
	ovs_add_flow "test_ct_connect_v4" ct4 \
		'in_port(2),eth(),eth_type(0x0806),arp()' '1' || return 1
	ovs_add_flow "test_ct_connect_v4" ct4 \
		     'ct_state(-trk),eth(),eth_type(0x0800),ipv4()' \
		     'ct(commit),recirc(0x1)' || return 1
	ovs_add_flow "test_ct_connect_v4" ct4 \
		     'recirc_id(0x1),ct_state(+trk+new),in_port(1),eth(),eth_type(0x0800),ipv4(src=172.31.110.10)' \
		     '2' || return 1
	ovs_add_flow "test_ct_connect_v4" ct4 \
		     'recirc_id(0x1),ct_state(+trk+est),in_port(1),eth(),eth_type(0x0800),ipv4(src=172.31.110.10)' \
		     '2' || return 1
	ovs_add_flow "test_ct_connect_v4" ct4 \
		     'recirc_id(0x1),ct_state(+trk+est),in_port(2),eth(),eth_type(0x0800),ipv4(dst=172.31.110.10)' \
		     '1' || return 1
	ovs_add_flow "test_ct_connect_v4" ct4 \
		     'recirc_id(0x1),ct_state(+trk+inv),eth(),eth_type(0x0800),ipv4()' 'drop' || \
		     return 1

	# do a ping
	ovs_sbx "test_ct_connect_v4" ip netns exec client ping 172.31.110.20 -c 3 || return 1

	# create an echo server in 'server'
	echo "server" | \
		ovs_netns_spawn_daemon "test_ct_connect_v4" "server" \
				nc -lvnp 4443
	ovs_sbx "test_ct_connect_v4" ip netns exec client nc -i 1 -zv 172.31.110.20 4443 || return 1

	# Now test in the other direction (should fail)
	echo "client" | \
		ovs_netns_spawn_daemon "test_ct_connect_v4" "client" \
				nc -lvnp 4443
	ovs_sbx "test_ct_connect_v4" ip netns exec client nc -i 1 -zv 172.31.110.10 4443
	if [ $? == 0 ]; then
	   info "ct connect to client was successful"
	   return 1
	fi

	info "done..."
	return 0
}

# connect_v4 test
#  - client has 1500 byte MTU
#  - server has 1500 byte MTU
#  - use ICMP to ping in each direction
test_connect_v4 () {

	sbx_add "test_connect_v4" || return $?

	ovs_add_dp "test_connect_v4" cv4 || return 1

	info "create namespaces"
	for ns in client server; do
		ovs_add_netns_and_veths "test_connect_v4" "cv4" "$ns" \
		    "${ns:0:1}0" "${ns:0:1}1" || return 1
	done


	ip netns exec client ip addr add 172.31.110.10/24 dev c1
	ip netns exec client ip link set c1 up
	ip netns exec server ip addr add 172.31.110.20/24 dev s1
	ip netns exec server ip link set s1 up

	# Add forwarding for ARP and ip packets - completely wildcarded
	ovs_add_flow "test_connect_v4" cv4 \
		'in_port(1),eth(),eth_type(0x0806),arp()' '2' || return 1
	ovs_add_flow "test_connect_v4" cv4 \
		'in_port(2),eth(),eth_type(0x0806),arp()' '1' || return 1
	ovs_add_flow "test_connect_v4" cv4 \
		'in_port(1),eth(),eth_type(0x0800),ipv4(src=172.31.110.10)' '2' || return 1
	ovs_add_flow "test_connect_v4" cv4 \
		'in_port(2),eth(),eth_type(0x0800),ipv4(src=172.31.110.20)' '1' || return 1

	# do a ping
	ovs_sbx "test_connect_v4" ip netns exec client ping 172.31.110.20 -c 3 || return 1

	info "done..."
	return 0
}

# nat_connect_v4 test
#  - client has 1500 byte MTU
#  - server has 1500 byte MTU
#  - use ICMP to ping in each direction
#  - only allow CT state stuff to pass through new in c -> s
test_nat_connect_v4 () {
	which nc >/dev/null 2>/dev/null || return $ksft_skip

	sbx_add "test_nat_connect_v4" || return $?

	ovs_add_dp "test_nat_connect_v4" nat4 || return 1
	info "create namespaces"
	for ns in client server; do
		ovs_add_netns_and_veths "test_nat_connect_v4" "nat4" "$ns" \
		    "${ns:0:1}0" "${ns:0:1}1" || return 1
	done

	ip netns exec client ip addr add 172.31.110.10/24 dev c1
	ip netns exec client ip link set c1 up
	ip netns exec server ip addr add 172.31.110.20/24 dev s1
	ip netns exec server ip link set s1 up

	ip netns exec client ip route add default via 172.31.110.20

	ovs_add_flow "test_nat_connect_v4" nat4 \
		'in_port(1),eth(),eth_type(0x0806),arp()' '2' || return 1
	ovs_add_flow "test_nat_connect_v4" nat4 \
		'in_port(2),eth(),eth_type(0x0806),arp()' '1' || return 1
	ovs_add_flow "test_nat_connect_v4" nat4 \
		"ct_state(-trk),in_port(1),eth(),eth_type(0x0800),ipv4(dst=192.168.0.20)" \
		"ct(commit,nat(dst=172.31.110.20)),recirc(0x1)"
	ovs_add_flow "test_nat_connect_v4" nat4 \
		"ct_state(-trk),in_port(2),eth(),eth_type(0x0800),ipv4()" \
		"ct(commit,nat),recirc(0x2)"

	ovs_add_flow "test_nat_connect_v4" nat4 \
		"recirc_id(0x1),ct_state(+trk-inv),in_port(1),eth(),eth_type(0x0800),ipv4()" "2"
	ovs_add_flow "test_nat_connect_v4" nat4 \
		"recirc_id(0x2),ct_state(+trk-inv),in_port(2),eth(),eth_type(0x0800),ipv4()" "1"

	# do a ping
	ovs_sbx "test_nat_connect_v4" ip netns exec client ping 192.168.0.20 -c 3 || return 1

	# create an echo server in 'server'
	echo "server" | \
		ovs_netns_spawn_daemon "test_nat_connect_v4" "server" \
				nc -lvnp 4443
	ovs_sbx "test_nat_connect_v4" ip netns exec client nc -i 1 -zv 192.168.0.20 4443 || return 1

	# Now test in the other direction (should fail)
	echo "client" | \
		ovs_netns_spawn_daemon "test_nat_connect_v4" "client" \
				nc -lvnp 4443
	ovs_sbx "test_nat_connect_v4" ip netns exec client nc -i 1 -zv 172.31.110.10 4443
	if [ $? == 0 ]; then
	   info "connect to client was successful"
	   return 1
	fi

	info "done..."
	return 0
}

# nat_related_v4 test
#  - client->server ip packets go via SNAT
#  - client solicits ICMP destination unreachable packet from server
#  - undo NAT for ICMP reply and test dst ip has been updated
test_nat_related_v4 () {
	which nc >/dev/null 2>/dev/null || return $ksft_skip

	sbx_add "test_nat_related_v4" || return $?

	ovs_add_dp "test_nat_related_v4" natrelated4 || return 1
	info "create namespaces"
	for ns in client server; do
		ovs_add_netns_and_veths "test_nat_related_v4" "natrelated4" "$ns" \
			"${ns:0:1}0" "${ns:0:1}1" || return 1
	done

	ip netns exec client ip addr add 172.31.110.10/24 dev c1
	ip netns exec client ip link set c1 up
	ip netns exec server ip addr add 172.31.110.20/24 dev s1
	ip netns exec server ip link set s1 up

	ip netns exec server ip route add 192.168.0.20/32 via 172.31.110.10

	# Allow ARP
	ovs_add_flow "test_nat_related_v4" natrelated4 \
		"in_port(1),eth(),eth_type(0x0806),arp()" "2" || return 1
	ovs_add_flow "test_nat_related_v4" natrelated4 \
		"in_port(2),eth(),eth_type(0x0806),arp()" "1" || return 1

	# Allow IP traffic from client->server, rewrite source IP with SNAT to 192.168.0.20
	ovs_add_flow "test_nat_related_v4" natrelated4 \
		"ct_state(-trk),in_port(1),eth(),eth_type(0x0800),ipv4(dst=172.31.110.20)" \
		"ct(commit,nat(src=192.168.0.20)),recirc(0x1)" || return 1
	ovs_add_flow "test_nat_related_v4" natrelated4 \
		"recirc_id(0x1),ct_state(+trk-inv),in_port(1),eth(),eth_type(0x0800),ipv4()" \
		"2" || return 1

	# Allow related ICMP responses back from server and undo NAT to restore original IP
	# Drop any ICMP related packets where dst ip hasn't been restored back to original IP
	ovs_add_flow "test_nat_related_v4" natrelated4 \
		"ct_state(-trk),in_port(2),eth(),eth_type(0x0800),ipv4()" \
		"ct(commit,nat),recirc(0x2)" || return 1
	ovs_add_flow "test_nat_related_v4" natrelated4 \
		"recirc_id(0x2),ct_state(+rel+trk),in_port(2),eth(),eth_type(0x0800),ipv4(src=172.31.110.20,dst=172.31.110.10,proto=1),icmp()" \
		"1" || return 1
	ovs_add_flow "test_nat_related_v4" natrelated4 \
		"recirc_id(0x2),ct_state(+rel+trk),in_port(2),eth(),eth_type(0x0800),ipv4(dst=192.168.0.20,proto=1),icmp()" \
		"drop" || return 1

	# Solicit destination unreachable response from server
	ovs_sbx "test_nat_related_v4" ip netns exec client \
		bash -c "echo a | nc -u -w 1 172.31.110.20 10000"

	# Check to make sure no packets matched the drop rule with incorrect dst ip
	python3 "$ovs_base/ovs-dpctl.py" dump-flows natrelated4 \
		| grep "drop" | grep "packets:0" >/dev/null || return 1

	info "done..."
	return 0
}

# netlink_validation
# - Create a dp
# - check no warning with "old version" simulation
test_netlink_checks () {
	sbx_add "test_netlink_checks" || return 1

	info "setting up new DP"
	ovs_add_dp "test_netlink_checks" nv0 || return 1
	# now try again
	PRE_TEST=$(dmesg | grep -E "RIP: [0-9a-fA-Fx]+:ovs_dp_cmd_new\+")
	ovs_add_dp "test_netlink_checks" nv0 -V 0 || return 1
	POST_TEST=$(dmesg | grep -E "RIP: [0-9a-fA-Fx]+:ovs_dp_cmd_new\+")
	if [ "$PRE_TEST" != "$POST_TEST" ]; then
		info "failed - gen warning"
		return 1
	fi

	ovs_add_netns_and_veths "test_netlink_checks" nv0 left left0 l0 || \
	    return 1
	ovs_add_netns_and_veths "test_netlink_checks" nv0 right right0 r0 || \
	    return 1
	[ $(python3 $ovs_base/ovs-dpctl.py show nv0 | grep port | \
	    wc -l) == 3 ] || \
	      return 1
	ovs_del_if "test_netlink_checks" nv0 right0 || return 1
	[ $(python3 $ovs_base/ovs-dpctl.py show nv0 | grep port | \
	    wc -l) == 2 ] || \
	      return 1

	info "Checking clone depth"
	ERR_MSG="Flow actions may not be safe on all matching packets"
	PRE_TEST=$(dmesg | grep -c "${ERR_MSG}")
	ovs_add_flow "test_netlink_checks" nv0 \
		'in_port(1),eth(),eth_type(0x800),ipv4()' \
		'clone(clone(clone(clone(clone(clone(clone(clone(clone(clone(clone(clone(clone(clone(clone(clone(clone(drop)))))))))))))))))' \
		>/dev/null 2>&1 && return 1
	POST_TEST=$(dmesg | grep -c "${ERR_MSG}")

	if [ "$PRE_TEST" == "$POST_TEST" ]; then
		info "failed - clone depth too large"
		return 1
	fi

	PRE_TEST=$(dmesg | grep -c "${ERR_MSG}")
	ovs_add_flow "test_netlink_checks" nv0 \
		'in_port(1),eth(),eth_type(0x0806),arp()' 'drop(0),2' \
		&> /dev/null && return 1
	POST_TEST=$(dmesg | grep -c "${ERR_MSG}")
	if [ "$PRE_TEST" == "$POST_TEST" ]; then
		info "failed - error not generated"
		return 1
	fi
	return 0
}

test_upcall_interfaces() {
	sbx_add "test_upcall_interfaces" || return 1

	info "setting up new DP"
	ovs_add_dp "test_upcall_interfaces" ui0 -V 2:1 || return 1

	ovs_add_netns_and_veths "test_upcall_interfaces" ui0 upc left0 l0 \
	    172.31.110.1/24 -u || return 1

	ovs_wait grep -q "listening on upcall packet handler" ${ovs_dir}/left0.out

	info "sending arping"
	ip netns exec upc arping -I l0 172.31.110.20 -c 1 \
	    >$ovs_dir/arping.stdout 2>$ovs_dir/arping.stderr

	grep -E "MISS upcall\[0/yes\]: .*arp\(sip=172.31.110.1,tip=172.31.110.20,op=1,sha=" $ovs_dir/left0.out >/dev/null 2>&1 || return 1
	return 0
}

ovs_add_kernel_tunnel() {
	local sbxname=$1; shift
	local ns=$1; shift
	local tnl_type=$1; shift
	local name=$1; shift
	local addr=$1; shift

	info "setting up kernel ${tnl_type} tunnel ${name}"
	ovs_sbx "${sbxname}" ip -netns ${ns} link add dev ${name} type ${tnl_type} $* || return 1
	on_exit "ovs_sbx ${sbxname} ip -netns ${ns} link del ${name} >/dev/null 2>&1"
	ovs_sbx "${sbxname}" ip -netns ${ns} addr add dev ${name} ${addr} || return 1
	ovs_sbx "${sbxname}" ip -netns ${ns} link set dev ${name} mtu 1450 up || return 1
}

test_tunnel_metadata() {
	which arping >/dev/null 2>&1 || return $ksft_skip

	sbxname="test_tunnel_metadata"
	sbx_add "${sbxname}" || return 1

	info "setting up new DP"
	ovs_add_dp "${sbxname}" tdp0 -V 2:1 || return 1

	ovs_add_netns_and_veths "${sbxname}" tdp0 tns left0 l0 \
		172.31.110.1/24 || return 1

	info "removing veth interface from openvswitch and setting IP"
	ovs_del_if "${sbxname}" tdp0 left0 || return 1
	ovs_sbx "${sbxname}" ip addr add 172.31.110.2/24 dev left0 || return 1
	ovs_sbx "${sbxname}" ip link set left0 up || return 1

	info "setting up tunnel port in openvswitch"
	ovs_add_if "${sbxname}" "vxlan" tdp0 ovs-vxlan0 -u || return 1
	on_exit "ovs_sbx ${sbxname} ip link del ovs-vxlan0"
	ovs_wait ip link show ovs-vxlan0 &>/dev/null || return 1
	ovs_sbx "${sbxname}" ip link set ovs-vxlan0 up || return 1

	configs=$(echo '
	    1 172.31.221.1/24 1155332 32   set   udpcsum flags\(df\|csum\)
	    2 172.31.222.1/24 1234567 45   set noudpcsum flags\(df\)
	    3 172.31.223.1/24 1020304 23 unset   udpcsum flags\(csum\)
	    4 172.31.224.1/24 1357986 15 unset noudpcsum' | sed '/^$/d')

	while read -r i addr id ttl df csum flags; do
		ovs_add_kernel_tunnel "${sbxname}" tns vxlan vxlan${i} ${addr} \
			remote 172.31.110.2 id ${id} dstport 4789 \
			ttl ${ttl} df ${df} ${csum} || return 1
	done <<< "${configs}"

	ovs_wait grep -q 'listening on upcall packet handler' \
		${ovs_dir}/ovs-vxlan0.out || return 1

	info "sending arping"
	for i in 1 2 3 4; do
		ovs_sbx "${sbxname}" ip netns exec tns \
			arping -I vxlan${i} 172.31.22${i}.2 -c 1 \
			>${ovs_dir}/arping.stdout 2>${ovs_dir}/arping.stderr
	done

	info "checking that received decapsulated packets carry correct metadata"
	while read -r i addr id ttl df csum flags; do
		arp_hdr="arp\\(sip=172.31.22${i}.1,tip=172.31.22${i}.2,op=1,sha="
		addrs="src=172.31.110.1,dst=172.31.110.2"
		ports="tp_src=[0-9]*,tp_dst=4789"
		tnl_md="tunnel\\(tun_id=${id},${addrs},ttl=${ttl},${ports},${flags}\\)"

		ovs_sbx "${sbxname}" grep -qE "MISS upcall.*${tnl_md}.*${arp_hdr}" \
			${ovs_dir}/ovs-vxlan0.out || return 1
	done <<< "${configs}"

	return 0
}

test_tunnel_refcount() {
	sbxname="test_tunnel_refcount"
	sbx_add "${sbxname}" || return 1

	ovs_sbx "${sbxname}" ip netns add trefns || return 1
	on_exit "ovs_sbx ${sbxname} ip netns del trefns"

	for tun_type in gre vxlan geneve; do
		info "testing ${tun_type} tunnel vport refcount"

		ovs_sbx "${sbxname}" ip netns exec trefns \
			python3 $ovs_base/ovs-dpctl.py \
			add-dp dp-${tun_type} || return 1

		ovs_sbx "${sbxname}" ip netns exec trefns \
			python3 $ovs_base/ovs-dpctl.py \
			add-if --no-lwt -t ${tun_type} \
			dp-${tun_type} ovs-${tun_type}0 || return 1

		ovs_wait ip -netns trefns link show \
			ovs-${tun_type}0 >/dev/null 2>&1 || return 1

		info "deleting dp - may hang if reference counting is broken"
		ovs_sbx "${sbxname}" ip netns exec trefns \
			python3 $ovs_base/ovs-dpctl.py \
			del-dp dp-${tun_type} &

		dev_removed() {
			! ip -netns trefns link show "$1" >/dev/null 2>&1
		}
		ovs_wait dev_removed dp-${tun_type} || return 1
		ovs_wait dev_removed ovs-${tun_type}0 || return 1
	done

	return 0
}

test_pop_vlan() {
	local sbx="test_pop_vlan"
	sbx_add "$sbx" || return $?
	ovs_add_dp "$sbx" vlandp || return 1

	ovs_add_netns_and_veths "$sbx" vlandp \
		ns1 veth1 ns1veth 192.0.2.1/24 || return 1
	ovs_add_netns_and_veths "$sbx" vlandp \
		ns2 veth2 ns2veth 192.0.2.2/24 || return 1

	# Baseline: untagged bidirectional forwarding
	ovs_add_flow "$sbx" vlandp \
		'in_port(1),eth(),eth_type(0x0806),arp()' '2' || return 1
	ovs_add_flow "$sbx" vlandp \
		'in_port(2),eth(),eth_type(0x0806),arp()' '1' || return 1
	ovs_add_flow "$sbx" vlandp \
		'in_port(1),eth(),eth_type(0x0800),ipv4()' '2' || return 1
	ovs_add_flow "$sbx" vlandp \
		'in_port(2),eth(),eth_type(0x0800),ipv4()' '1' || return 1
	ovs_sbx "$sbx" ip netns exec ns1 ping -c 3 -W 2 \
		192.0.2.2 || return 1

	# VLAN topology: ns1 uses VLAN sub-interface, ns2 is plain
	ip -n ns1 link add link ns1veth name ns1veth.10 \
		type vlan id 10 || return 1
	on_exit "ip -n ns1 link del ns1veth.10 2>/dev/null"
	ip -n ns1 addr add 198.51.100.1/24 dev ns1veth.10 || return 1
	ip -n ns1 link set ns1veth.10 up || return 1
	ip -n ns2 addr add 198.51.100.2/24 dev ns2veth || return 1

	ovs_del_flows "$sbx" vlandp

	# Static ARP: avoids VLAN-tagged ARP complexity
	local ns1veth10mac ns2mac
	ns1veth10mac=$(ip -n ns1 link show ns1veth.10 \
		| awk '/link\/ether/ {print $2}')
	[ -z "$ns1veth10mac" ] && \
		{ info "failed to get ns1veth10mac"; return 1; }
	ns2mac=$(ip -n ns2 link show ns2veth \
		| awk '/link\/ether/ {print $2}')
	[ -z "$ns2mac" ] && \
		{ info "failed to get ns2mac"; return 1; }
	ip -n ns1 neigh replace 198.51.100.2 lladdr "$ns2mac" \
		dev ns1veth.10 nud permanent || return 1
	ip -n ns2 neigh replace 198.51.100.1 \
		lladdr "$ns1veth10mac" \
		dev ns2veth nud permanent || return 1

	local vlan_match='in_port(1),eth(),eth_type(0x8100),'
	vlan_match+='vlan(vid=10),'
	vlan_match+='encap(eth_type(0x0800),'
	vlan_match+='ipv4(src=198.51.100.1,proto=1),icmp())'

	# Negative: forward without pop_vlan -- tagged frame
	# is invisible to ns2 (no VLAN sub-interface), ping fails
	ovs_add_flow "$sbx" vlandp "$vlan_match" '2' || return 1
	ovs_sbx "$sbx" ip netns exec ns1 ping -I ns1veth.10 \
		-c 3 -W 1 198.51.100.2 >/dev/null 2>&1 \
		&& { info "FAIL: ping should fail without pop_vlan"
		     return 1; }

	ovs_del_flows "$sbx" vlandp

	# Positive: pop_vlan strips tag on forward path,
	# push_vlan restores tag on return path -- ping succeeds
	ovs_add_flow "$sbx" vlandp \
		"$vlan_match" 'pop_vlan,2' || return 1
	ovs_add_flow "$sbx" vlandp \
		'in_port(2),eth(),eth_type(0x0800),ipv4()' \
		'push_vlan(vid=10,pcp=0,tpid=0x8100),1' || return 1
	ovs_sbx "$sbx" ip netns exec ns1 ping -I ns1veth.10 \
		-c 3 -W 2 198.51.100.2 || return 1

	return 0
}

run_test() {
	(
	tname="$1"
	tdesc="$2"

	if python3 ovs-dpctl.py -h 2>&1 | \
	     grep -E "Need to (install|upgrade) the python" >/dev/null 2>&1; then
		stdbuf -o0 printf "TEST: %-60s  [PYLIB]\n" "${tdesc}"
		return $ksft_skip
	fi

	python3 ovs-dpctl.py show >/dev/null 2>&1 || \
		echo "[DPCTL] show exception."

	if ! lsmod | grep openvswitch >/dev/null 2>&1; then
		stdbuf -o0 printf "TEST: %-60s  [NOMOD]\n" "${tdesc}"
		return $ksft_skip
	fi

	printf "TEST: %-60s  [START]\n" "${tname}"

	unset IFS

	eval test_${tname}
	ret=$?

	if [ $ret -eq 0 ]; then
		printf "TEST: %-60s  [ OK ]\n" "${tdesc}"
		ovs_exit_sig
		rm -rf "$ovs_dir"
	elif [ $ret -eq 1 ]; then
		printf "TEST: %-60s  [FAIL]\n" "${tdesc}"
		if [ "${PAUSE_ON_FAIL}" = "yes" ]; then
			echo
			echo "Pausing. Logs in $ovs_dir/. Hit enter to continue"
			read a
		fi
		ovs_exit_sig
		[ "${PAUSE_ON_FAIL}" = "yes" ] || rm -rf "$ovs_dir"
		exit 1
	elif [ $ret -eq $ksft_skip ]; then
		printf "TEST: %-60s  [SKIP]\n" "${tdesc}"
	elif [ $ret -eq 2 ]; then
		rm -rf test_${tname}
		run_test "$1" "$2"
	fi

	return $ret
	)
	ret=$?
	case $ret in
		0)
			[ $all_skipped = true ] && [ $exitcode=$ksft_skip ] && exitcode=0
			all_skipped=false
		;;
		$ksft_skip)
			[ $all_skipped = true ] && exitcode=$ksft_skip
		;;
		*)
			all_skipped=false
			exitcode=1
		;;
	esac

	return $ret
}


exitcode=0
desc=0
all_skipped=true

while getopts :pvt o
do
	case $o in
	p) PAUSE_ON_FAIL=yes;;
	v) VERBOSE=1;;
	t) if which tcpdump > /dev/null 2>&1; then
		TRACING=1
	   else
		echo "=== tcpdump not available, tracing disabled"
	   fi
	   ;;
	*) usage;;
	esac
done
shift $(($OPTIND-1))

IFS="	
"

for arg do
	# Check first that all requested tests are available before running any
	command -v > /dev/null "test_${arg}" || { echo "=== Test ${arg} not found"; usage; }
done

name=""
desc=""
for t in ${tests}; do
	[ "${name}" = "" ]	&& name="${t}"	&& continue
	[ "${desc}" = "" ]	&& desc="${t}"

	run_this=1
	for arg do
		[ "${arg}" != "${arg#--*}" ] && continue
		[ "${arg}" = "${name}" ] && run_this=1 && break
		run_this=0
	done
	if [ $run_this -eq 1 ]; then
		run_test "${name}" "${desc}"
	fi
	name=""
	desc=""
done

exit ${exitcode}
