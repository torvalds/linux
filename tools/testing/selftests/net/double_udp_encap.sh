#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

source lib.sh

# shellcheck disable=SC2155 # prefer RO variable over return value from cmd
readonly CLI="$(dirname "$(readlink -f "$0")")/../../../net/ynl/pyynl/cli.py"

readonly SRC=1
readonly DST=2

readonly NET_V4=192.168.1.
readonly NET_V6=2001:db8::
readonly OL1_NET_V4=172.16.1.
readonly OL1_NET_V6=2001:db8:1::
readonly OL2_NET_V4=172.16.2.
readonly OL2_NET_V6=2001:db8:2::

trap cleanup_all_ns EXIT

# shellcheck disable=SC2329 # can't figure out usage trough a variable
is_ipv6() {
	if [[ $1 =~ .*:.* ]]; then
		return 0
	fi
	return 1
}

# shellcheck disable=SC2329 # can't figure out usage trough a variable
create_gnv_endpoint() {
	local -r netns=$1
	local -r bm_rem_addr=$2
	local -r gnv_dev=$3
	local -r gnv_id=$4
	local opts=$5
	local gnv_json
	local rem

	if is_ipv6 "$bm_rem_addr"; then
		rem=remote6
	else
		rem=remote
	fi

	# add ynl opt separator, if needed
	[ -n "$opts" ] && opts=", $opts"

	gnv_json="{ \"id\": $gnv_id, \"$rem\": \"$bm_rem_addr\"$opts }"
	ip netns exec "$netns" "$CLI" --family rt-link --create --excl \
	   --do newlink  --json "{\"ifname\": \"$gnv_dev\",
	       \"linkinfo\": {\"kind\":\"geneve\",
	       \"data\": $gnv_json } }" > /dev/null
	ip -n "$netns" link set dev "$gnv_dev" up
}

# shellcheck disable=SC2329 # can't figure out usage trough a variable
create_vxlan_endpoint() {
	local -r netns=$1
	local -r bm_rem_addr=$2
	local -r vxlan_dev=$3
	local -r vxlan_id=$4
	local -r opts_str=$5
	local oldifs
	local -a opts
	local opt

	# convert the arguments from yaml format
	oldifs=$IFS
	IFS=','
	for opt in $opts_str; do
		local pattern='"port":'

		[ -n "$opt" ] || continue

		opts+=("${opt/$pattern*/dstport}" "${opt/$pattern/}")
	done
	IFS=$oldifs
	[ ${#opts[@]} -gt 0 ] || opts+=("dstport" "4789")

	ip -n "$netns" link add "$vxlan_dev" type vxlan id "$vxlan_id" \
	   remote "$bm_rem_addr" "${opts[@]}"
	ip -n "$netns" link set dev "$vxlan_dev" up
}

create_ns() {
	local nested_opt='"port":6082'
	local create_endpoint
	local options="$1"
	local feature
	local dev
	local id
	local ns

	RET=0

	#  +-------------+    +-------------+
	#  | NS_SRC      |    | NS_NST_DST  |
	#  |             |    |             |
	#  |   gnv_nst1  |    |  gnv_nst2   |
	#  |   +         |    |         +   |
	#  |   |         |    |         |   |
	#  |   +         |    |         +   |
	#  |  gnv1       |    |        gnv2 |
	#  |   +         |    |         +   |
	#  |   |         |    |         |   |
	#  |   + veth1 +--------+ veth2 +   |
	#  |             |    |             |
	#  +-------------+    +-------------+

	setup_ns NS_SRC NS_DST

	# concatenate caller provided options and default one
	[ -n "$2" ] && nested_opt="$nested_opt,$2"

	ip link add name "veth$SRC" netns "$NS_SRC" type veth \
	   peer name "veth$DST" netns "$NS_DST"
	case "$ENCAP" in
	vxlan)
		create_endpoint=create_vxlan_endpoint
		dev=vx
		;;
	geneve)
		create_endpoint=create_gnv_endpoint
		dev=gnv
		;;
	esac

	id=1
	for ns in "${NS_LIST[@]}"; do
		ip -n "$ns" link set dev "veth$id" up

		# ensure the sender can do large write just after 3whs
		ip netns exec "$ns" \
		   sysctl -qw net.ipv4.tcp_wmem="4096 4194304 4194304"

		# note that 3 - $SRC == $DST and 3 - $DST == $SRC
		if [ $FAMILY = "4" ]; then
			ip -n "$ns" addr add dev "veth$id" "$NET_V4$id/24"
			$create_endpoint "$ns" "$NET_V4$((3 - id))" \
			   "$dev$id" 4 "$options"
			ip -n "$ns" addr add dev "$dev$id" "$OL1_NET_V4$id/24"

			# nested tunnel devices
			# pmtu can't be propagated to upper layer devices;
			# need manual adjust
			$create_endpoint "$ns" "$OL1_NET_V4$((3 - id))" \
			   "$dev"_nst"$id" 40 "$nested_opt"
			ip -n "$ns" addr add dev "$dev"_nst"$id" \
			   "$OL2_NET_V4$id/24"
			ip -n "$ns" link set dev "$dev"_nst"$id" mtu 1392
		else
			ip -n "$ns" addr add dev "veth$id" "$NET_V6$id/64" \
			   nodad
			$create_endpoint "$ns" "$NET_V6$((3 - id))" \
			   "$dev"6"$id" 6 "$options"
			ip -n "$ns" addr add dev "$dev"6"$id" \
			   "$OL1_NET_V6$id/64" nodad

			$create_endpoint "$ns" "$OL1_NET_V6$((3 - id))" \
			   "$dev"6_nst"$id" 60 "$nested_opt"
			ip -n "$ns" addr add dev "$dev"6_nst"$id" \
			   "$OL2_NET_V6$id/64" nodad
			ip -n "$ns" link set dev "$dev"6_nst"$id" mtu 1352
		fi
		id=$((id+1))
	done

	# enable GRO heuristic on the veth peer and ensure UDP L4 over tunnel is
	# actually segmented
	for feature in tso tx-udp_tnl-segmentation; do
		ip netns exec "$NS_SRC" ethtool -K "veth$SRC" \
		   "$feature" off 2>/dev/null
	done
}

create_ns_gso() {
	local dev

	create_ns "$@"
	if [ "$ENCAP" = "geneve" ]; then
		dev=gnv
	else
		dev=vx
	fi
	[ "$FAMILY" = "6" ] && dev="$dev"6
	ip netns exec "$NS_SRC" ethtool -K "$dev$SRC" \
	   tx-gso-partial on \
	   tx-udp_tnl-segmentation on \
	   tx-udp_tnl-csum-segmentation on
}

create_ns_gso_gro() {
	create_ns_gso "$@"
	ip netns exec "$NS_DST" ethtool -K "veth$DST" gro on
	ip netns exec "$NS_SRC" ethtool -K "veth$SRC" tx off >/dev/null 2>&1
}

run_test() {
	local -r dst=$NET$DST
	local -r msg=$1
	local -r total_size=$2
	local -r encappkts=$3
	local inner_proto_offset=0
	local inner_maclen=14
	local rx_family="-4"
	local ipt=iptables
	local bpf_filter
	local -a rx_args
	local wire_pkts
	local rcvpkts
	local encl=8
	local dport
	local pkts
	local snd

	if [ $FAMILY = "6" ]; then
		ipt=ip6tables
	else
		# rx program does not support '-6' and implies ipv6 usage by
		# default
		rx_args=("$rx_family")
	fi

	# The received can only check fixed size packet
	pkts=$((total_size / GSO_SIZE))
	if [ -n "$4" ]; then
		wire_pkts=$4
	elif [ $((total_size % GSO_SIZE)) -eq 0 ]; then
		wire_pkts=1
		rx_args+=("-l" "$GSO_SIZE")
	else
		wire_pkts=2
		pkts=$((pkts + 1))
	fi

	if [ "$ENCAP" = "geneve" ]; then
		dport=6081
	else
		dport=4789
	fi

	# Either:
	# - IPv4, nested tunnel carries UDP over IPv4, with dport 6082,
	#   innermost is TCP over IPv4 on port 8000
	# - IPv6, nested tunnel carries UDP over IPv6, with dport 6082,
	#   innermost is TCP over IPv6 on port 8000
	# The nested tunnel port is 6082 and the nested encap len is 8
	# regardless of the encap type (no geneve opts).
	# In inherit protocol mode there is no nested mac hdr and the nested
	# l3 protocol type field belongs to the geneve hdr.
	[ "$USE_HINT" = true ] && encl=16
	[ "$INHERIT" = true ] && inner_maclen=0
	[ "$INHERIT" = true ] && inner_proto_offset=-4
	local inner=$((inner_maclen+encl))
	local proto=$((inner_maclen+encl+inner_proto_offset))
	bpf_filter=$(nfbpf_compile "(ip &&
		ip[$((40+encl))] == 0x08 && ip[$((41+encl))] == 0x00 &&
		ip[$((51+encl))] == 0x11 &&
		ip[$((64+encl))] == 0x17 && ip[$((65+encl))] == 0xc2 &&
		ip[$((76+proto))] == 0x08 && ip[$((77+proto))] == 0x00 &&
		ip[$((87+inner))] == 0x6 &&
		ip[$((100+inner))] == 0x1f && ip[$((101+inner))] == 0x40) ||
		(ip6 &&
		ip6[$((60+encl))] == 0x86 && ip6[$((61+encl))] == 0xdd &&
		ip6[$((68+encl))] == 0x11 &&
		ip6[$((104+encl))] == 0x17 && ip6[$((105+encl))] == 0xc2 &&
		ip6[$((116+proto))] == 0x86 && ip6[$((117+proto))] == 0xdd &&
		ip6[$((124+inner))] == 0x6 &&
		ip6[$((160+inner))] == 0x1f && ip6[$((161+inner))] == 0x40)")

	# ignore shorts packet, to avoid arp/mld induced noise
	ip netns exec "$NS_SRC" "$ipt" -A OUTPUT -p udp --dport "$dport" \
	   -m length --length 600:65535 -m bpf --bytecode "$bpf_filter"
	ip netns exec "$NS_DST" "$ipt" -A INPUT -p udp --dport "$dport" \
	   -m length --length 600:65535 -m bpf --bytecode "$bpf_filter"
	ip netns exec "$NS_DST" ./udpgso_bench_rx -C 2000 -t -R 100 \
	   -n "$pkts" "${rx_args[@]}" &
	local pid=$!
	wait_local_port_listen "$NS_DST" 8000 tcp
	ip netns exec "$NS_SRC" ./udpgso_bench_tx -"$FAMILY" -t -M 1 \
	   -s "$total_size" -D "$dst"
	local ret=$?
	check_err "$ret" "client failure exit code $ret"
	wait "$pid"
	ret=$?
	check_err "$ret" "sever failure exit code $ret"

	snd=$(ip netns exec "$NS_SRC" "$ipt"-save -c |
	    grep "dport $dport" | sed -e 's/\[//' -e 's/:.*//')

	[ "$snd" = "$wire_pkts" ]
	# shellcheck disable=SC2319 # known false positive
	check_err $? "send $snd packets on the lowest link, expected $wire_pkts"

	rcvpkts=$(ip netns exec "$NS_DST" "$ipt"-save -c | \
	   grep "dport $dport" | sed -e 's/\[//' -e 's/:.*//')

	[ "$rcvpkts" = "$encappkts" ]
	check_err $? "received $rcvpkts $ENCAP packets, expected $encappkts"
	log_test "$msg"
}

run_tests() {
	for FAMILY in 4 6; do
		NET=$OL2_NET_V4
		GSO_SIZE=1340 # 1392 - 20 - 32

		if [ $FAMILY = 6 ]; then
			NET=$OL2_NET_V6
			GSO_SIZE=1280 # 1352 - 40 - 32
		fi

		echo "IPv$FAMILY"

		unset USE_HINT
		unset INHERIT

		# "geneve" must be last encap in list, so that later
		# test cases will run on it
		for ENCAP in "vxlan" "geneve"; do
			create_ns
			run_test "No GSO - $ENCAP" $((GSO_SIZE * 4)) 4 4
			cleanup_all_ns

			create_ns_gso
			run_test "GSO without GRO - $ENCAP" $((GSO_SIZE * 4)) \
			   4 1
			cleanup_all_ns

			# IPv4 only test
			[ $FAMILY = "4" ] || continue
			create_ns_gso
			ip netns exec "$NS_SRC" \
			   sysctl -qw net.ipv4.ip_no_pmtu_disc=1
			run_test "GSO disable due to no fixedid - $ENCAP" \
			   $((GSO_SIZE * 4)) 4 4
			cleanup_all_ns
		done

		# GRO tests imply/require geneve encap, the only one providing
		# GRO hints
		create_ns_gso_gro
		run_test "double tunnel GRO, no hints" $((GSO_SIZE * 4)) 4
		cleanup_all_ns

		# hint option is expected for all the following tests in the RX
		# path
		USE_HINT=true
		create_ns_gso_gro \
		   '"gro-hint":1,"udp-zero-csum6-tx":1,"udp-zero-csum6-rx":1' \
		   '"udp-zero-csum6-tx":1,"udp-zero-csum6-rx":1'
		run_test "double tunnel GRO" $((GSO_SIZE * 4)) 1
		cleanup_all_ns

		create_ns_gso_gro '"gro-hint":1,"udp-csum":1' '"udp-csum":1'
		run_test "double tunnel GRO - csum complete" $((GSO_SIZE * 4))\
		   1
		cleanup_all_ns

		create_ns_gso_gro '"gro-hint":1' \
		   '"udp-csum":0,"udp-zero-csum6-tx":1,"udp-zero-csum6-rx":1'
		run_test "double tunnel GRO - no nested csum" \
		   $((GSO_SIZE * 4)) 1
		cleanup_all_ns

		create_ns_gso_gro \
		   '"gro-hint":1,"udp-zero-csum6-tx":1,"udp-zero-csum6-rx":1' \
		   '"udp-csum":1'
		run_test "double tunnel GRO - nested csum, outer 0-csum, skip"\
		   $((GSO_SIZE * 4)) 4
		cleanup_all_ns

		INHERIT=true
		create_ns_gso_gro '"gro-hint":1,"udp-csum":1' \
		   '"udp-csum":1,"inner-proto-inherit":1'
		run_test "double tunnel GRO - nested inherit proto" \
		   $((GSO_SIZE * 4)) 1
		cleanup_all_ns
		unset INHERIT

		create_ns_gso_gro '"gro-hint":1'
		run_test "double tunnel GRO - short last pkt" \
		   $((GSO_SIZE * 4 + GSO_SIZE / 2)) 2
		cleanup_all_ns
	done
}

require_command nfbpf_compile
require_command jq

# tcp retransmisions will break the accounting
xfail_on_slow run_tests
exit "$EXIT_STATUS"
