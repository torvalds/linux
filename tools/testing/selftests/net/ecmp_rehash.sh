#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test local ECMP path re-selection on TCP retransmission timeout and PLB.
#
# Two namespaces connected by two parallel veth pairs with a 2-way ECMP
# route.  When a TCP path is blocked (via tc drop) or congested (via
# netem ECN marking), the kernel rehashes the connection via
# sk_rethink_txhash() + __sk_dst_reset(), causing the next route lookup
# to select the other ECMP path.
#
# Expected runtime: ~60 seconds.  Most time is spent waiting for TCP
# retransmission timeouts (1-7s per test) and running multi-round
# consistency checks (10 rounds each).  The large slowwait/connect-timeout
# values (30-120s) are worst-case bounds for CI; a correctly functioning
# kernel reaches each check well before the timeout expires.

source lib.sh

SUBNETS=(a b)
PORT=9900
: "${ECMP_REBUILD_ROUNDS:=10}"

# alloc_ports NAME [COUNT]: set NAME to the next free port and reserve
# COUNT ports (default 1) from a shared counter.  Each test allocates its
# own port(s) where it runs, so a retry or a newly added test never
# collides; the per-round tests reserve ECMP_REBUILD_ROUNDS each.
NEXT_PORT=$PORT
alloc_ports()
{
	printf -v "$1" '%d' "$NEXT_PORT"
	NEXT_PORT=$((NEXT_PORT + ${2:-1}))
}

ALL_TESTS="
	test_ecmp_syn_rehash
	test_ecmp_synack_rehash
	test_ecmp_midstream_rehash
	test_ecmp_midstream_ack_rehash
	test_ecmp_plb_rehash
	test_ecmp_hash_policy1_no_rehash
	test_ecmp_no_flowlabel_leak
	test_ecmp_dst_rebuild_consistency
	test_ecmp_syncookie_path_consistency
"

link_tx_packets_get()
{
	local ns=$1; shift
	local dev=$1; shift

	ip netns exec "$ns" cat "/sys/class/net/$dev/statistics/tx_packets"
}

# Return the number of packets matched by the tc filter action on a device.
# When tc drops packets via "action drop", the device's tx_packets is not
# incremented (packet never reaches veth_xmit), but the tc action maintains
# its own counter.
tc_filter_pkt_count()
{
	local ns=$1; shift
	local dev=$1; shift

	ip netns exec "$ns" tc -s filter show dev "$dev" parent 1: 2>/dev/null |
		awk '/Sent .* pkt/ {
			for (i=1; i<=NF; i++)
				if ($i == "pkt") { print $(i-1); exit }
		}'
}

# Read a TcpExt counter from /proc/net/netstat in a namespace.
# Returns 0 if the counter is not found.
get_netstat_counter()
{
	local ns=$1; shift
	local field=$1; shift
	local val

	# shellcheck disable=SC2016
	val=$(ip netns exec "$ns" awk -v key="$field" '
		/^TcpExt:/ {
			if (!h) { split($0, n); h=1 }
			else {
				split($0, v)
				for (i in n)
					if (n[i] == key) print v[i]
			}
		}
	' /proc/net/netstat)
	echo "${val:-0}"
}

# Apply netem ECN marking: CE-mark all ECT packets instead of dropping them.
mark_ecn()
{
	local ns=$1; shift
	local dev=$1; shift

	ip netns exec "$ns" tc qdisc add dev "$dev" root netem loss 100% ecn
}

# Block TCP (IPv6 next-header = 6) egress, allowing ICMPv6 through.
block_tcp()
{
	local ns=$1; shift
	local dev=$1; shift

	ip netns exec "$ns" tc qdisc add dev "$dev" root handle 1: prio
	ip netns exec "$ns" tc filter add dev "$dev" parent 1: \
		protocol ipv6 prio 1 u32 match u8 0x06 0xff at 6 action drop
}

unblock_tcp()
{
	local ns=$1; shift
	local dev=$1; shift

	ip netns exec "$ns" tc qdisc del dev "$dev" root 2>/dev/null
}

# Return success when a device's TX counter exceeds a baseline value.
dev_tx_packets_above()
{
	local ns=$1; shift
	local dev=$1; shift
	local baseline=$1; shift

	local cur
	cur=$(link_tx_packets_get "$ns" "$dev")
	[ "$cur" -gt "$baseline" ]
}

# Return success when both devices have dropped at least one TCP packet.
both_devs_attempted()
{
	local ns=$1; shift
	local dev0=$1; shift
	local dev1=$1; shift

	local c0 c1
	c0=$(tc_filter_pkt_count "$ns" "$dev0")
	c1=$(tc_filter_pkt_count "$ns" "$dev1")
	[ "${c0:-0}" -ge 1 ] && [ "${c1:-0}" -ge 1 ]
}

link_tx_packets_total()
{
	local ns=$1; shift
	local dev0=${1:-veth0a}; shift 2>/dev/null
	local dev1=${1:-veth1a}

	echo $(( $(link_tx_packets_get "$ns" "$dev0") +
		 $(link_tx_packets_get "$ns" "$dev1") ))
}

# (Re)install the ECMP multipath routes between NS1 and NS2.  $1 is the
# ip route operation ("add" to create, "change" to replace).  If $2 is
# given it names a congestion control to pin on both routes via "congctl";
# because dctcp carries TCP_CONG_NEEDS_ECN, this also tags the route with
# DST_FEATURE_ECN_CA, which makes the server negotiate ECN without the
# listener itself having to run dctcp.  The nexthop topology lives here
# only, so a test can re-pin the routes and restore them with one call.
install_ecmp_routes()
{
	local op=$1 cc=$2
	local -a cc_attr=()

	[ -n "$cc" ] && cc_attr=(congctl "$cc")

	ip -n "$NS1" -6 route "$op" fd00:ff::2/128 "${cc_attr[@]}" \
		nexthop via fd00:a::2 dev veth0a \
		nexthop via fd00:b::2 dev veth1a

	ip -n "$NS2" -6 route "$op" fd00:ff::1/128 "${cc_attr[@]}" \
		nexthop via fd00:a::1 dev veth0b \
		nexthop via fd00:b::1 dev veth1b
}

setup()
{
	setup_ns NS1 NS2

	local ns
	for ns in "$NS1" "$NS2"; do
		ip netns exec "$ns" sysctl -qw net.ipv6.conf.all.accept_dad=0
		ip netns exec "$ns" sysctl -qw net.ipv6.conf.default.accept_dad=0
		ip netns exec "$ns" sysctl -qw net.ipv6.conf.all.forwarding=1
		ip netns exec "$ns" sysctl -qw net.core.txrehash=1
	done

	local i sub
	for i in 0 1; do
		sub=${SUBNETS[$i]}
		ip link add "veth${i}a" type veth peer name "veth${i}b"
		ip link set "veth${i}a" netns "$NS1"
		ip link set "veth${i}b" netns "$NS2"
		ip -n "$NS1" addr add "fd00:${sub}::1/64" dev "veth${i}a"
		ip -n "$NS2" addr add "fd00:${sub}::2/64" dev "veth${i}b"
		ip -n "$NS1" link set "veth${i}a" up
		ip -n "$NS2" link set "veth${i}b" up
	done

	ip -n "$NS1" addr add fd00:ff::1/128 dev lo
	ip -n "$NS2" addr add fd00:ff::2/128 dev lo

	# Allow many SYN retries at 1-second intervals (linear, no
	# exponential backoff) so the rehash test has enough attempts
	# to exercise both ECMP paths.
	if ! ip netns exec "$NS1" sysctl -qw \
	     net.ipv4.tcp_syn_linear_timeouts=25; then
		echo "SKIP: tcp_syn_linear_timeouts not supported"
		return "$ksft_skip"
	fi
	ip netns exec "$NS1" sysctl -qw net.ipv4.tcp_syn_retries=25

	# Keep the server's request socket alive during the blocking
	# period so SYN/ACK retransmits continue.
	ip netns exec "$NS2" sysctl -qw net.ipv4.tcp_synack_retries=25

	install_ecmp_routes add

	for i in 0 1; do
		sub=${SUBNETS[$i]}
		ip netns exec "$NS1" \
			ping -6 -c1 -W5 "fd00:${sub}::2" &>/dev/null
		ip netns exec "$NS2" \
			ping -6 -c1 -W5 "fd00:${sub}::1" &>/dev/null
	done

	if ! ip netns exec "$NS1" ping -6 -c1 -W5 fd00:ff::2 &>/dev/null; then
		echo "Basic connectivity check failed"
		return "$ksft_skip"
	fi
}

# Block ALL paths, start a connection, wait until SYNs have been dropped
# on both interfaces (proving rehash steered the SYN to a new path), then
# unblock so the connection completes.
test_ecmp_syn_rehash()
{
	RET=0
	local port
	alloc_ports port

	block_tcp "$NS1" veth0a
	defer unblock_tcp "$NS1" veth0a
	block_tcp "$NS1" veth1a
	defer unblock_tcp "$NS1" veth1a

	ip netns exec "$NS2" socat \
		"TCP6-LISTEN:$port,bind=[fd00:ff::2],reuseaddr,fork" \
		EXEC:"echo ESTABLISH_OK" &
	defer kill_process $!

	wait_local_port_listen "$NS2" "$port" tcp

	local rehash_before
	rehash_before=$(get_netstat_counter "$NS1" TcpTimeoutRehash)

	# Start the connection in the background; it will retry SYNs at
	# 1-second intervals until an unblocked path is found.
	# Use -u (unidirectional) to only receive from the server;
	# sending data back would risk SIGPIPE if the server's EXEC
	# child has already exited.
	local tmpfile
	tmpfile=$(mktemp)
	defer rm -f "$tmpfile"

	ip netns exec "$NS1" socat -u \
		"TCP6:[fd00:ff::2]:$port,bind=[fd00:ff::1],connect-timeout=60" \
		STDOUT >"$tmpfile" 2>&1 &
	local client_pid=$!
	defer kill_process "$client_pid"

	# Wait until both paths have seen at least one dropped SYN.
	# This proves sk_rethink_txhash() rehashed the connection from
	# one ECMP path to the other.
	slowwait 30 both_devs_attempted "$NS1" veth0a veth1a > /dev/null
	check_err $? "SYNs did not appear on both paths (rehash not working)"
	if [ "$RET" -ne 0 ]; then
		log_test "Local ECMP SYN rehash: establish with blocked paths"
		return
	fi

	# Unblock both paths and let the next SYN retransmit succeed.
	unblock_tcp "$NS1" veth0a
	unblock_tcp "$NS1" veth1a

	local rc=0
	wait "$client_pid" || rc=$?

	local result
	result=$(cat "$tmpfile" 2>/dev/null)

	if [[ "$result" != *"ESTABLISH_OK"* ]]; then
		check_err 1 "connection failed after unblocking (rc=$rc): $result"
	fi

	local rehash_after
	rehash_after=$(get_netstat_counter "$NS1" TcpTimeoutRehash)
	if [ "$rehash_after" -le "$rehash_before" ]; then
		check_err 1 "TcpTimeoutRehash counter did not increment"
	fi

	log_test "Local ECMP SYN rehash: establish with blocked paths"
}

# Block the server's return paths so SYN/ACKs are dropped.  The client
# retransmits SYNs at 1-second intervals; each duplicate SYN arriving at
# the server triggers tcp_rtx_synack() which re-rolls txhash, so the
# retransmitted SYN/ACK selects a different ECMP return path.
test_ecmp_synack_rehash()
{
	RET=0
	local port
	alloc_ports port

	block_tcp "$NS2" veth0b
	defer unblock_tcp "$NS2" veth0b
	block_tcp "$NS2" veth1b
	defer unblock_tcp "$NS2" veth1b

	ip netns exec "$NS2" socat \
		"TCP6-LISTEN:$port,bind=[fd00:ff::2],reuseaddr,fork" \
		EXEC:"echo SYNACK_OK" &
	defer kill_process $!

	wait_local_port_listen "$NS2" "$port" tcp

	# Start the connection; SYNs reach the server (client egress is
	# open) but SYN/ACKs are dropped on the server's return path.
	local tmpfile
	tmpfile=$(mktemp)
	defer rm -f "$tmpfile"

	ip netns exec "$NS1" socat -u \
		"TCP6:[fd00:ff::2]:$port,bind=[fd00:ff::1],connect-timeout=60" \
		STDOUT >"$tmpfile" 2>&1 &
	local client_pid=$!
	defer kill_process "$client_pid"

	# Wait until both server-side interfaces have dropped at least
	# one SYN/ACK, proving the server rehashed its return path.
	slowwait 30 both_devs_attempted "$NS2" veth0b veth1b > /dev/null
	check_err $? "SYN/ACKs did not appear on both return paths"
	if [ "$RET" -ne 0 ]; then
		log_test "Local ECMP SYN/ACK rehash: blocked return path"
		return
	fi

	# Unblock and let the connection complete.
	unblock_tcp "$NS2" veth0b
	unblock_tcp "$NS2" veth1b

	local rc=0
	wait "$client_pid" || rc=$?

	local result
	result=$(cat "$tmpfile" 2>/dev/null)

	if [[ "$result" != *"SYNACK_OK"* ]]; then
		check_err 1 "connection failed after unblocking (rc=$rc): $result"
	fi

	log_test "Local ECMP SYN/ACK rehash: blocked return path"
}

# Establish a data transfer with both paths open, then block the
# active path.  Verify that data appears on the previously inactive
# path (proving RTO triggered a rehash) and that TcpTimeoutRehash
# incremented.
#
# With 2-way ECMP each rehash may pick the same path, so a single
# attempt can occasionally fail.  Retry once for robustness.

# Single attempt at the midstream rehash check.  Returns 0 on success.
ecmp_midstream_rehash_attempt()
{
	local port=$1; shift
	local reason=""

	ip netns exec "$NS2" socat -u \
		"TCP6-LISTEN:$port,bind=[fd00:ff::2],reuseaddr" - >/dev/null &
	local server_pid=$!

	wait_local_port_listen "$NS2" "$port" tcp

	local base_tx0 base_tx1
	base_tx0=$(link_tx_packets_get "$NS1" veth0a)
	base_tx1=$(link_tx_packets_get "$NS1" veth1a)

	# Continuous data source; timeout caps overall test duration and
	# must exceed the slowwait below so data keeps flowing.
	ip netns exec "$NS1" timeout 90 socat -u \
		OPEN:/dev/zero \
		"TCP6:[fd00:ff::2]:$port,bind=[fd00:ff::1]" &>/dev/null &
	local client_pid=$!

	# Wait for enough packets to identify the active path.
	if ! busywait "$BUSYWAIT_TIMEOUT" until_counter_is \
			">= $((base_tx0 + base_tx1 + 10))" \
		link_tx_packets_total "$NS1" > /dev/null; then
		kill "$client_pid" "$server_pid" 2>/dev/null
		wait "$client_pid" "$server_pid" 2>/dev/null
		echo "no TX activity"
		return 1
	fi

	# Find the active path and block it.
	local current_tx0 current_tx1 active_idx inactive_idx
	current_tx0=$(link_tx_packets_get "$NS1" veth0a)
	current_tx1=$(link_tx_packets_get "$NS1" veth1a)
	if [ $((current_tx0 - base_tx0)) -ge $((current_tx1 - base_tx1)) ]; then
		active_idx=0; inactive_idx=1
	else
		active_idx=1; inactive_idx=0
	fi

	local rehash_before
	rehash_before=$(get_netstat_counter "$NS1" TcpTimeoutRehash)
	# Suppress __dst_negative_advice() in tcp_write_timeout() so
	# that __sk_dst_reset() is the only dst-invalidation mechanism
	# on the RTO path.
	local saved_retries1
	saved_retries1=$(ip netns exec "$NS1" sysctl -n net.ipv4.tcp_retries1)
	ip netns exec "$NS1" sysctl -qw net.ipv4.tcp_retries1=255

	block_tcp "$NS1" "veth${active_idx}a"

	# Capture baseline after block_tcp returns.  block_tcp adds a
	# prio qdisc then a tc filter; between those two steps the
	# qdisc's CAN_BYPASS fast-path lets packets through unfiltered.
	local inactive_before
	inactive_before=$(link_tx_packets_get "$NS1" "veth${inactive_idx}a")

	# Wait for meaningful data on the previously inactive path,
	# proving RTO triggered a rehash and data actually moved.
	if ! slowwait 60 dev_tx_packets_above \
		"$NS1" "veth${inactive_idx}a" "$((inactive_before + 100))" \
		> /dev/null; then
		reason="no data on alternate path"
	fi

	local rehash_after
	rehash_after=$(get_netstat_counter "$NS1" TcpTimeoutRehash)
	if [ "$rehash_after" -le "$rehash_before" ]; then
		reason="${reason:+$reason; }TcpTimeoutRehash did not increment"
	fi

	unblock_tcp "$NS1" "veth${active_idx}a"
	ip netns exec "$NS1" sysctl -qw \
		net.ipv4.tcp_retries1="$saved_retries1"
	kill "$client_pid" "$server_pid" 2>/dev/null
	wait "$client_pid" "$server_pid" 2>/dev/null
	if [ -n "$reason" ]; then
		echo "$reason"
		return 1
	fi
	return 0
}

test_ecmp_midstream_rehash()
{
	RET=0
	local port retry_port
	alloc_ports port
	alloc_ports retry_port

	local fail_reason
	if ! ecmp_midstream_rehash_attempt "$port" >/dev/null; then
		fail_reason=$(ecmp_midstream_rehash_attempt "$retry_port")
		check_err $? "$fail_reason"
	fi

	log_test "Local ECMP midstream rehash: block active path"
}

# Single attempt at the ACK rehash check.  Returns 0 on success.
ecmp_ack_rehash_attempt()
{
	local port=$1; shift
	local reason=""

	ip netns exec "$NS2" socat -u \
		"TCP6-LISTEN:$port,bind=[fd00:ff::2],reuseaddr" - >/dev/null &
	local server_pid=$!

	wait_local_port_listen "$NS2" "$port" tcp

	local base_tx0 base_tx1
	base_tx0=$(link_tx_packets_get "$NS2" veth0b)
	base_tx1=$(link_tx_packets_get "$NS2" veth1b)

	# Continuous data source from NS1 to NS2.  Cap the send buffer
	# so in-flight data stays below the receiver's advertised window.
	# Without this, the sender can exhaust the receiver's window and
	# enter persist mode (zero-window probing) instead of RTO when
	# ACKs are blocked, and persist probes do not trigger flowlabel
	# rehash.
	ip netns exec "$NS1" timeout 120 socat -u \
		OPEN:/dev/zero \
		"TCP6:[fd00:ff::2]:$port,bind=[fd00:ff::1],sndbuf=16384" \
		&>/dev/null &
	local client_pid=$!

	# Wait for enough server TX (ACKs) to identify the active return path.
	if ! busywait "$BUSYWAIT_TIMEOUT" until_counter_is \
			">= $((base_tx0 + base_tx1 + 10))" \
		link_tx_packets_total "$NS2" veth0b veth1b > /dev/null; then
		kill "$client_pid" "$server_pid" 2>/dev/null
		wait "$client_pid" "$server_pid" 2>/dev/null
		echo "no server TX activity"
		return 1
	fi

	local cur_tx0 cur_tx1 active_dev inactive_dev
	cur_tx0=$(link_tx_packets_get "$NS2" veth0b)
	cur_tx1=$(link_tx_packets_get "$NS2" veth1b)
	if [ $((cur_tx0 - base_tx0)) -ge $((cur_tx1 - base_tx1)) ]; then
		active_dev=veth0b; inactive_dev=veth1b
	else
		active_dev=veth1b; inactive_dev=veth0b
	fi

	local rehash_before
	rehash_before=$(get_netstat_counter "$NS2" TcpDuplicateDataRehash)

	# Block the inactive return path first (no effect on current
	# ACK flow), then block the active path.  This avoids counting
	# normal ACK drops as rehash evidence.
	block_tcp "$NS2" "$inactive_dev"
	local inactive_before
	inactive_before=$(tc_filter_pkt_count "$NS2" "$inactive_dev")
	block_tcp "$NS2" "$active_dev"

	# NS1 will RTO (no ACKs), retransmit with new flowlabel.
	# NS2 detects the flowlabel change via tcp_rcv_spurious_retrans(),
	# rehashes, and NS2's ACKs try the previously inactive return
	# path.  One successful rehash is sufficient.
	if ! slowwait 60 until_counter_is \
			">= $((${inactive_before:-0} + 1))" \
		tc_filter_pkt_count "$NS2" "$inactive_dev" > /dev/null; then
		reason="no ACKs on alternate return path after blocking"
	fi

	local rehash_after
	rehash_after=$(get_netstat_counter "$NS2" TcpDuplicateDataRehash)
	if [ "$rehash_after" -le "$rehash_before" ]; then
		reason="${reason:+$reason; }TcpDuplicateDataRehash did not increment"
	fi

	unblock_tcp "$NS2" "$active_dev"
	unblock_tcp "$NS2" "$inactive_dev"
	kill "$client_pid" "$server_pid" 2>/dev/null
	wait "$client_pid" "$server_pid" 2>/dev/null
	if [ -n "$reason" ]; then
		echo "$reason"
		return 1
	fi
	return 0
}

# Block the receiver's (NS2) ACK return paths while data flows from
# NS1 to NS2.  The sender (NS1) times out and retransmits with a new
# flowlabel; the receiver detects the changed flowlabel via
# tcp_rcv_spurious_retrans() and rehashes its own txhash so that its
# ACKs try a different ECMP return path.
#
# With 2-way ECMP each rehash may pick the same path, so a single
# attempt can occasionally fail.  Retry once for robustness.
test_ecmp_midstream_ack_rehash()
{
	RET=0
	local port retry_port
	alloc_ports port
	alloc_ports retry_port

	local fail_reason
	if ! ecmp_ack_rehash_attempt "$port" >/dev/null; then
		fail_reason=$(ecmp_ack_rehash_attempt "$retry_port")
		check_err $? "$fail_reason"
	fi

	log_test "Local ECMP midstream ACK rehash: blocked return path"
}

# Establish a DCTCP data transfer with PLB enabled, then ECN-mark both
# paths.  Sustained CE marking triggers PLB to call sk_rethink_txhash()
# + __sk_dst_reset(), bouncing the connection between ECMP paths.
# Verify data appears on both paths and that TCPPLBRehash incremented.
test_ecmp_plb_rehash()
{
	RET=0
	local port
	alloc_ports port

	# PLB needs DCTCP, a restricted congestion control.  Adding it to
	# the host-global tcp_allowed_congestion_control would relax the
	# restricted-CC policy for the whole host (there is no per-netns
	# allowed set).  Instead pin dctcp on the test routes with
	# "congctl": the route's RTAX_CC_ALGO is honoured on both the
	# connect and accept paths without the restricted-CC check, and a
	# dctcp route also carries DST_FEATURE_ECN_CA so the server
	# negotiates ECN -- all confined to the test namespaces.
	local available
	available=$(ip netns exec "$NS1" sysctl -n \
		net.ipv4.tcp_available_congestion_control)
	if ! echo "$available" | grep -qw dctcp; then
		log_test_skip "Local ECMP PLB rehash: DCTCP not available"
		return "$ksft_skip"
	fi
	install_ecmp_routes change dctcp
	defer install_ecmp_routes change

	# Save NS1 sysctls before modifying them.
	local saved_ecn1 saved_plb_enabled saved_plb_rounds
	local saved_plb_thresh saved_plb_suspend
	saved_ecn1=$(ip netns exec "$NS1" sysctl -n net.ipv4.tcp_ecn)
	saved_plb_enabled=$(ip netns exec "$NS1" sysctl -n net.ipv4.tcp_plb_enabled)
	saved_plb_rounds=$(ip netns exec "$NS1" sysctl -n net.ipv4.tcp_plb_rehash_rounds)
	saved_plb_thresh=$(ip netns exec "$NS1" sysctl -n net.ipv4.tcp_plb_cong_thresh)
	saved_plb_suspend=$(ip netns exec "$NS1" sysctl -n net.ipv4.tcp_plb_suspend_rto_sec)

	# Enable ECN and PLB on the sender; dctcp comes from the route.
	ip netns exec "$NS1" sysctl -qw net.ipv4.tcp_ecn=1
	ip netns exec "$NS1" sysctl -qw net.ipv4.tcp_plb_enabled=1
	ip netns exec "$NS1" sysctl -qw net.ipv4.tcp_plb_rehash_rounds=3
	ip netns exec "$NS1" sysctl -qw net.ipv4.tcp_plb_cong_thresh=1
	ip netns exec "$NS1" sysctl -qw net.ipv4.tcp_plb_suspend_rto_sec=0
	defer ip netns exec "$NS1" sysctl -qw net.ipv4.tcp_ecn="$saved_ecn1"
	defer ip netns exec "$NS1" sysctl -qw net.ipv4.tcp_plb_enabled="$saved_plb_enabled"
	defer ip netns exec "$NS1" sysctl -qw net.ipv4.tcp_plb_rehash_rounds="$saved_plb_rounds"
	defer ip netns exec "$NS1" sysctl -qw net.ipv4.tcp_plb_cong_thresh="$saved_plb_thresh"
	defer ip netns exec "$NS1" sysctl -qw net.ipv4.tcp_plb_suspend_rto_sec="$saved_plb_suspend"

	ip netns exec "$NS2" socat -u \
		"TCP6-LISTEN:$port,bind=[fd00:ff::2],reuseaddr" - >/dev/null &
	defer kill_process $!

	wait_local_port_listen "$NS2" "$port" tcp

	local base_tx0 base_tx1
	base_tx0=$(link_tx_packets_get "$NS1" veth0a)
	base_tx1=$(link_tx_packets_get "$NS1" veth1a)

	ip netns exec "$NS1" timeout 90 socat -u \
		OPEN:/dev/zero \
		"TCP6:[fd00:ff::2]:$port,bind=[fd00:ff::1]" &>/dev/null &
	local client_pid=$!
	defer kill_process "$client_pid"

	# Wait for data to start flowing before applying ECN marking.
	busywait "$BUSYWAIT_TIMEOUT" until_counter_is \
			">= $((base_tx0 + base_tx1 + 10))" \
		link_tx_packets_total "$NS1" > /dev/null
	check_err $? "no TX activity detected"
	if [ "$RET" -ne 0 ]; then
		log_test "Local ECMP PLB rehash: ECN-marked path"
		return
	fi

	# Snapshot TX counters and rehash stats before ECN marking.
	local pre_ecn_tx0 pre_ecn_tx1
	pre_ecn_tx0=$(link_tx_packets_get "$NS1" veth0a)
	pre_ecn_tx1=$(link_tx_packets_get "$NS1" veth1a)

	local plb_before rto_before
	plb_before=$(get_netstat_counter "$NS1" TCPPLBRehash)
	rto_before=$(get_netstat_counter "$NS1" TcpTimeoutRehash)

	# CE-mark all data on both paths.  PLB detects sustained
	# congestion and rehashes, bouncing traffic between paths.
	mark_ecn "$NS1" veth0a
	defer unblock_tcp "$NS1" veth0a	# removes the marking rule
	mark_ecn "$NS1" veth1a
	defer unblock_tcp "$NS1" veth1a	# removes the marking rule

	# Wait for meaningful data on both paths, proving PLB rehashed
	# the connection and traffic actually moved.  Require at least
	# 100 packets beyond the baseline to rule out stray control
	# packets (ND, etc.) satisfying the check.
	slowwait 60 dev_tx_packets_above \
		"$NS1" veth0a "$((pre_ecn_tx0 + 100))" > /dev/null
	check_err $? "no data on veth0a after ECN marking"

	slowwait 60 dev_tx_packets_above \
		"$NS1" veth1a "$((pre_ecn_tx1 + 100))" > /dev/null
	check_err $? "no data on veth1a after ECN marking"

	local plb_after rto_after
	plb_after=$(get_netstat_counter "$NS1" TCPPLBRehash)
	rto_after=$(get_netstat_counter "$NS1" TcpTimeoutRehash)
	if [ "$plb_after" -le "$plb_before" ]; then
		check_err 1 "TCPPLBRehash counter did not increment"
	fi
	if [ "$rto_after" -gt "$rto_before" ]; then
		check_err 1 "TcpTimeoutRehash incremented; rehash was RTO-driven, not PLB"
	fi

	log_test "Local ECMP PLB rehash: ECN-marked path"
}

# Verify that hash policy 1 (L3+L4 symmetric) preserves the ECMP path
# across rehash.  Policy 1 computes a deterministic hash from the
# 5-tuple, so mp_hash stays 0 and rt6_multipath_hash() always selects
# the same path regardless of txhash changes.
test_ecmp_hash_policy1_no_rehash()
{
	RET=0
	local port
	alloc_ports port

	local saved_policy
	saved_policy=$(ip netns exec "$NS1" sysctl -n \
		net.ipv6.fib_multipath_hash_policy)
	ip netns exec "$NS1" sysctl -qw net.ipv6.fib_multipath_hash_policy=1
	defer ip netns exec "$NS1" sysctl -qw \
		net.ipv6.fib_multipath_hash_policy="$saved_policy"

	block_tcp "$NS1" veth0a
	defer unblock_tcp "$NS1" veth0a
	block_tcp "$NS1" veth1a
	defer unblock_tcp "$NS1" veth1a

	ip netns exec "$NS2" socat \
		"TCP6-LISTEN:$port,bind=[fd00:ff::2],reuseaddr,fork" \
		EXEC:"echo POLICY1_OK" &
	defer kill_process $!

	wait_local_port_listen "$NS2" "$port" tcp

	local rehash_before
	rehash_before=$(get_netstat_counter "$NS1" TcpTimeoutRehash)

	ip netns exec "$NS1" timeout 10 socat -u \
		"TCP6:[fd00:ff::2]:$port,bind=[fd00:ff::1],connect-timeout=8" \
		STDOUT >/dev/null 2>&1 &
	local client_pid=$!
	defer kill_process "$client_pid"

	# With policy 1, the deterministic 5-tuple hash always selects
	# the same path.  Wait for multiple SYN retransmits (proving
	# rehash was attempted), then verify all SYNs landed on the
	# same interface.
	local rehash_after
	slowwait 8 until_counter_is ">= $((rehash_before + 3))" \
		get_netstat_counter "$NS1" TcpTimeoutRehash > /dev/null
	rehash_after=$(get_netstat_counter "$NS1" TcpTimeoutRehash)
	if [ "$rehash_after" -le "$rehash_before" ]; then
		check_err 1 "TcpTimeoutRehash counter did not increment"
	fi

	local c0 c1
	c0=$(tc_filter_pkt_count "$NS1" veth0a)
	c1=$(tc_filter_pkt_count "$NS1" veth1a)
	if [ "${c0:-0}" -ge 1 ] && [ "${c1:-0}" -ge 1 ]; then
		check_err 1 "SYNs appeared on both paths despite policy 1"
	fi
	if [ "${c0:-0}" -eq 0 ] && [ "${c1:-0}" -eq 0 ]; then
		check_err 1 "no SYNs observed on either path"
	fi

	log_test "Local ECMP policy 1: no path change on rehash"
}

# Verify that mp_hash does not leak into the on-wire flowlabel.
# With auto_flowlabels=0, the wire flowlabel must be 0.  Install tc
# filters that pass TCP with flowlabel=0 but drop TCP with nonzero
# flowlabel, then establish a connection and transfer data.  If
# mp_hash leaked into fl6->flowlabel, the SYN or data packets would
# be dropped and the connection would fail.
test_ecmp_no_flowlabel_leak()
{
	RET=0
	local port
	alloc_ports port

	local saved_afl
	saved_afl=$(ip netns exec "$NS1" sysctl -n \
		net.ipv6.auto_flowlabels)
	ip netns exec "$NS1" sysctl -qw net.ipv6.auto_flowlabels=0
	defer ip netns exec "$NS1" sysctl -qw \
		net.ipv6.auto_flowlabels="$saved_afl"

	# On both egress interfaces: pass TCP with flowlabel=0 (prio 1),
	# drop any remaining TCP (nonzero flowlabel, prio 2).  ICMPv6
	# matches neither filter and passes through normally.
	local dev
	for dev in veth0a veth1a; do
		ip netns exec "$NS1" tc qdisc add dev "$dev" \
			root handle 1: prio
		ip netns exec "$NS1" tc filter add dev "$dev" parent 1: \
			protocol ipv6 prio 1 u32 \
			match u32 0x00000000 0x000FFFFF at 0 \
			match u8 0x06 0xff at 6 \
			action ok
		ip netns exec "$NS1" tc filter add dev "$dev" parent 1: \
			protocol ipv6 prio 2 u32 \
			match u8 0x06 0xff at 6 \
			action drop
		defer unblock_tcp "$NS1" "$dev"
	done

	ip netns exec "$NS2" socat \
		"TCP6-LISTEN:$port,bind=[fd00:ff::2],reuseaddr" \
		EXEC:"echo FLOWLABEL_OK" &
	defer kill_process $!

	wait_local_port_listen "$NS2" "$port" tcp

	local tmpfile
	tmpfile=$(mktemp)
	defer rm -f "$tmpfile"

	ip netns exec "$NS1" socat -u \
		"TCP6:[fd00:ff::2]:$port,bind=[fd00:ff::1],connect-timeout=10" \
		STDOUT >"$tmpfile" 2>&1

	local result
	result=$(cat "$tmpfile" 2>/dev/null)
	if [[ "$result" != *"FLOWLABEL_OK"* ]]; then
		check_err 1 "connection failed: mp_hash may have leaked into wire flowlabel"
	fi

	log_test "No flowlabel leak with auto_flowlabels=0"
}

# Helper: stream data, invalidate the cached dst by adding and
# removing a dummy route (bumps fib6_node sernum), then check that
# traffic stays on the same ECMP path.  Used by both the normal
# tcp_v6_connect and syncookie variants.
ecmp_dst_rebuild_check()
{
	local ns_client=$1; shift
	local port=$1; shift
	local rc=0

	# Suppress __dst_negative_advice() during the test so that a
	# real TCP timeout cannot trigger an additional dst
	# invalidation via a different code path.
	local saved_retries1
	saved_retries1=$(ip netns exec "$ns_client" sysctl -n \
		net.ipv4.tcp_retries1)
	ip netns exec "$ns_client" sysctl -qw net.ipv4.tcp_retries1=255

	local base0 base1
	base0=$(link_tx_packets_get "$ns_client" veth0a)
	base1=$(link_tx_packets_get "$ns_client" veth1a)

	ip netns exec "$ns_client" timeout 15 socat -u \
		OPEN:/dev/zero \
		"TCP6:[fd00:ff::2]:$port,bind=[fd00:ff::1]" \
		&>/dev/null &
	local client_pid=$!

	# Wait for enough packets to identify the active path.
	# Return 2 for setup failure (distinct from 1 = path changed).
	if ! busywait "$BUSYWAIT_TIMEOUT" until_counter_is \
			">= $((base0 + base1 + 50))" \
		link_tx_packets_total "$ns_client" > /dev/null; then
		ip netns exec "$ns_client" sysctl -qw \
			net.ipv4.tcp_retries1="$saved_retries1"
		kill "$client_pid" 2>/dev/null
		wait "$client_pid" 2>/dev/null
		return 2
	fi

	local mid0 mid1 active_dev inactive_dev
	mid0=$(link_tx_packets_get "$ns_client" veth0a)
	mid1=$(link_tx_packets_get "$ns_client" veth1a)
	if [ $((mid0 - base0)) -ge $((mid1 - base1)) ]; then
		active_dev=veth0a; inactive_dev=veth1a
	else
		active_dev=veth1a; inactive_dev=veth0a
	fi

	local active_before inactive_before
	active_before=$(link_tx_packets_get "$ns_client" "$active_dev")
	inactive_before=$(link_tx_packets_get "$ns_client" "$inactive_dev")

	# Invalidate the cached dst by bumping the fib6_node sernum.
	# Adding and removing a high-metric dummy route achieves this
	# without touching the ECMP nexthops, avoiding a transient
	# single-nexthop state during multipath route replace.
	ip -n "$ns_client" -6 route add fd00:ff::2/128 dev lo metric 9999
	ip -n "$ns_client" -6 route del fd00:ff::2/128 dev lo metric 9999

	# Wait for enough post-rebuild traffic to detect a path change.
	if ! busywait "$BUSYWAIT_TIMEOUT" until_counter_is \
			">= $((active_before + inactive_before + 50))" \
		link_tx_packets_total "$ns_client" > /dev/null; then
		ip netns exec "$ns_client" sysctl -qw \
			net.ipv4.tcp_retries1="$saved_retries1"
		kill "$client_pid" 2>/dev/null
		wait "$client_pid" 2>/dev/null
		return 2
	fi

	local active_after inactive_after
	active_after=$(link_tx_packets_get "$ns_client" "$active_dev")
	inactive_after=$(link_tx_packets_get "$ns_client" "$inactive_dev")

	local active_delta=$((active_after - active_before))
	local inactive_delta=$((inactive_after - inactive_before))

	if [ "$inactive_delta" -gt "$active_delta" ]; then
		rc=1
	fi

	ip netns exec "$ns_client" sysctl -qw \
		net.ipv4.tcp_retries1="$saved_retries1"
	kill "$client_pid" 2>/dev/null
	wait "$client_pid" 2>/dev/null
	return "$rc"
}

# Run ecmp_dst_rebuild_check for ECMP_REBUILD_ROUNDS rounds, each with
# a fresh server and connection.  With a correct kernel the path is
# deterministic (same txhash always selects the same ECMP nexthop),
# so any path change is a bug.  Multiple rounds catch a buggy kernel
# that picks a random path: each round has 50% chance of accidentally
# matching, so 10 rounds gives < 0.1% false-pass probability.
ecmp_dst_rebuild_loop()
{
	local base_port=$1; shift
	local label=$1; shift
	local path_changes=0
	local r

	for r in $(seq 1 "$ECMP_REBUILD_ROUNDS"); do
		local port=$((base_port + r - 1))

		ip netns exec "$NS2" socat -u \
			"TCP6-LISTEN:$port,bind=[fd00:ff::2],reuseaddr" \
			- >/dev/null &
		local server_pid=$!

		wait_local_port_listen "$NS2" "$port" tcp

		local check_rc=0
		ecmp_dst_rebuild_check "$NS1" "$port" || check_rc=$?

		kill "$server_pid" 2>/dev/null
		wait "$server_pid" 2>/dev/null

		busywait "$BUSYWAIT_TIMEOUT" \
			port_has_no_active_tcp "$NS1" "$port" > /dev/null
		busywait "$BUSYWAIT_TIMEOUT" \
			port_has_no_active_tcp "$NS2" "$port" > /dev/null

		if [ "$check_rc" -eq 2 ]; then
			check_err 1 "no TX activity in round $r"
			break
		elif [ "$check_rc" -eq 1 ]; then
			path_changes=$((path_changes + 1))
		fi
	done

	if [ "$path_changes" -gt 0 ]; then
		check_err 1 "$path_changes/$ECMP_REBUILD_ROUNDS changed path"
	fi

	log_test "$label"
}

# Verify that a dst invalidation does not cause the connection to
# switch ECMP paths.  With the fix, both the initial route lookup
# (tcp_v6_connect) and subsequent rebuilds (inet6_csk_route_socket)
# use sk_txhash >> 1, so the path is stable.
test_ecmp_dst_rebuild_consistency()
{
	RET=0
	local base_port
	alloc_ports base_port "$ECMP_REBUILD_ROUNDS"

	ecmp_dst_rebuild_loop "$base_port" \
		"ECMP path stable after dst invalidation"
}

# Return 0 (true) when no active TCP sockets remain on a port.
# TIME_WAIT is excluded because it does not generate outgoing traffic.
port_has_no_active_tcp()
{
	local ns=$1; shift
	local port=$1; shift

	! ip netns exec "$ns" ss -tnH \
		state established \
		state fin-wait-1 \
		state fin-wait-2 \
		state close-wait \
		state last-ack \
		state closing \
		state syn-sent \
		state syn-recv \
		"sport = :$port or dport = :$port" | grep -q .
}

# Count TCP packets on server egress without blocking them.
# Uses tc filters with "action ok" so packets are counted and passed.
count_tcp()
{
	local ns=$1; shift
	local dev=$1; shift

	ip netns exec "$ns" tc qdisc add dev "$dev" root handle 1: prio
	ip netns exec "$ns" tc filter add dev "$dev" parent 1: \
		protocol ipv6 prio 1 u32 match u8 0x06 0xff at 6 action ok
}

# Verify that the server's SYN-ACK (sent from the request socket) and
# subsequent ACKs (sent from the full socket created in cookie_v6_check)
# use the same ECMP path.  With syncookies the request socket is freed
# after the SYN-ACK and a new one is created during cookie validation;
# this test catches the case where the two request sockets pick
# different ECMP paths due to independent txhash values.
test_ecmp_syncookie_path_consistency()
{
	RET=0

	local saved_syncookies
	saved_syncookies=$(ip netns exec "$NS2" sysctl -n \
		net.ipv4.tcp_syncookies 2>/dev/null)
	if [ -z "$saved_syncookies" ]; then
		log_test_skip "Syncookie server ECMP path consistent"
		return "$ksft_skip"
	fi
	ip netns exec "$NS2" sysctl -qw net.ipv4.tcp_syncookies=2
	defer ip netns exec "$NS2" sysctl -qw \
		net.ipv4.tcp_syncookies="$saved_syncookies"

	count_tcp "$NS2" veth0b
	defer unblock_tcp "$NS2" veth0b
	count_tcp "$NS2" veth1b
	defer unblock_tcp "$NS2" veth1b

	local path_splits=0
	local r base_port
	alloc_ports base_port "$ECMP_REBUILD_ROUNDS"

	for r in $(seq 1 "$ECMP_REBUILD_ROUNDS"); do
		local port=$((base_port + r - 1))

		ip netns exec "$NS2" socat -u \
			"TCP6-LISTEN:$port,bind=[fd00:ff::2],reuseaddr" \
			- >/dev/null &
		local server_pid=$!

		wait_local_port_listen "$NS2" "$port" tcp

		local srv_base0 srv_base1
		srv_base0=$(tc_filter_pkt_count "$NS2" veth0b)
		srv_base1=$(tc_filter_pkt_count "$NS2" veth1b)

		ip netns exec "$NS1" timeout 5 socat -u \
			OPEN:/dev/zero \
			"TCP6:[fd00:ff::2]:$port,bind=[fd00:ff::1]" \
			&>/dev/null &
		local client_pid=$!

		local cli_base
		cli_base=$(link_tx_packets_total "$NS1")
		if ! busywait "$BUSYWAIT_TIMEOUT" until_counter_is \
				">= $((cli_base + 200))" \
			link_tx_packets_total "$NS1" > /dev/null; then
			check_err 1 "no TX activity in round $r"
			kill "$client_pid" 2>/dev/null
			wait "$client_pid" 2>/dev/null
			kill "$server_pid" 2>/dev/null
			wait "$server_pid" 2>/dev/null
			break
		fi

		local srv_tcp0 srv_tcp1
		srv_tcp0=$(tc_filter_pkt_count "$NS2" veth0b)
		srv_tcp1=$(tc_filter_pkt_count "$NS2" veth1b)
		local srv_delta0=$(( ${srv_tcp0:-0} - ${srv_base0:-0} ))
		local srv_delta1=$(( ${srv_tcp1:-0} - ${srv_base1:-0} ))

		if [ "$srv_delta0" -gt 0 ] && [ "$srv_delta1" -gt 0 ]; then
			path_splits=$((path_splits + 1))
		fi

		kill "$client_pid" 2>/dev/null
		wait "$client_pid" 2>/dev/null
		kill "$server_pid" 2>/dev/null
		wait "$server_pid" 2>/dev/null

		# Wait for TCP teardown packets (FIN/RST) to finish so
		# they do not pollute the next round's tc filter counters.
		busywait "$BUSYWAIT_TIMEOUT" \
			port_has_no_active_tcp "$NS1" "$port" > /dev/null
		busywait "$BUSYWAIT_TIMEOUT" \
			port_has_no_active_tcp "$NS2" "$port" > /dev/null
	done

	if [ "$path_splits" -gt 0 ]; then
		check_err 1 "$path_splits/$ECMP_REBUILD_ROUNDS had split server path"
	fi

	log_test "Syncookie server ECMP path consistent"
}

require_command socat

trap 'defer_scopes_cleanup; cleanup_all_ns' EXIT
setup || exit $?
tests_run
exit "$EXIT_STATUS"
