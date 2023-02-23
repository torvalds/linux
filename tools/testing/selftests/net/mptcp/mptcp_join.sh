#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Double quotes to prevent globbing and word splitting is recommended in new
# code but we accept it, especially because there were too many before having
# address all other issues detected by shellcheck.
#shellcheck disable=SC2086

ret=0
sin=""
sinfail=""
sout=""
cin=""
cinfail=""
cinsent=""
cout=""
capout=""
ns1=""
ns2=""
ksft_skip=4
timeout_poll=30
timeout_test=$((timeout_poll * 2 + 1))
capture=0
checksum=0
ip_mptcp=0
check_invert=0
validate_checksum=0
init=0
evts_ns1=""
evts_ns2=""
evts_ns1_pid=0
evts_ns2_pid=0

declare -A all_tests
declare -a only_tests_ids
declare -a only_tests_names
declare -A failed_tests
TEST_COUNT=0
TEST_NAME=""
nr_blank=40

export FAILING_LINKS=""

# generated using "nfbpf_compile '(ip && (ip[54] & 0xf0) == 0x30) ||
#				  (ip6 && (ip6[74] & 0xf0) == 0x30)'"
CBPF_MPTCP_SUBOPTION_ADD_ADDR="14,
			       48 0 0 0,
			       84 0 0 240,
			       21 0 3 64,
			       48 0 0 54,
			       84 0 0 240,
			       21 6 7 48,
			       48 0 0 0,
			       84 0 0 240,
			       21 0 4 96,
			       48 0 0 74,
			       84 0 0 240,
			       21 0 1 48,
			       6 0 0 65535,
			       6 0 0 0"

init_partial()
{
	capout=$(mktemp)

	local sec rndh
	sec=$(date +%s)
	rndh=$(printf %x $sec)-$(mktemp -u XXXXXX)

	ns1="ns1-$rndh"
	ns2="ns2-$rndh"

	local netns
	for netns in "$ns1" "$ns2"; do
		ip netns add $netns || exit $ksft_skip
		ip -net $netns link set lo up
		ip netns exec $netns sysctl -q net.mptcp.enabled=1
		ip netns exec $netns sysctl -q net.mptcp.pm_type=0
		ip netns exec $netns sysctl -q net.ipv4.conf.all.rp_filter=0
		ip netns exec $netns sysctl -q net.ipv4.conf.default.rp_filter=0
		if [ $checksum -eq 1 ]; then
			ip netns exec $netns sysctl -q net.mptcp.checksum_enabled=1
		fi
	done

	check_invert=0
	validate_checksum=$checksum
	FAILING_LINKS=""

	#  ns1         ns2
	# ns1eth1    ns2eth1
	# ns1eth2    ns2eth2
	# ns1eth3    ns2eth3
	# ns1eth4    ns2eth4

	local i
	for i in $(seq 1 4); do
		ip link add ns1eth$i netns "$ns1" type veth peer name ns2eth$i netns "$ns2"
		ip -net "$ns1" addr add 10.0.$i.1/24 dev ns1eth$i
		ip -net "$ns1" addr add dead:beef:$i::1/64 dev ns1eth$i nodad
		ip -net "$ns1" link set ns1eth$i up

		ip -net "$ns2" addr add 10.0.$i.2/24 dev ns2eth$i
		ip -net "$ns2" addr add dead:beef:$i::2/64 dev ns2eth$i nodad
		ip -net "$ns2" link set ns2eth$i up

		# let $ns2 reach any $ns1 address from any interface
		ip -net "$ns2" route add default via 10.0.$i.1 dev ns2eth$i metric 10$i
		ip -net "$ns2" route add default via dead:beef:$i::1 dev ns2eth$i metric 10$i
	done
}

init_shapers()
{
	local i
	for i in $(seq 1 4); do
		tc -n $ns1 qdisc add dev ns1eth$i root netem rate 20mbit delay 1
		tc -n $ns2 qdisc add dev ns2eth$i root netem rate 20mbit delay 1
	done
}

cleanup_partial()
{
	rm -f "$capout"

	local netns
	for netns in "$ns1" "$ns2"; do
		ip netns del $netns
		rm -f /tmp/$netns.{nstat,out}
	done
}

check_tools()
{
	if ! ip -Version &> /dev/null; then
		echo "SKIP: Could not run test without ip tool"
		exit $ksft_skip
	fi

	if ! iptables -V &> /dev/null; then
		echo "SKIP: Could not run all tests without iptables tool"
		exit $ksft_skip
	fi

	if ! ip6tables -V &> /dev/null; then
		echo "SKIP: Could not run all tests without ip6tables tool"
		exit $ksft_skip
	fi
}

init() {
	init=1

	check_tools

	sin=$(mktemp)
	sout=$(mktemp)
	cin=$(mktemp)
	cinsent=$(mktemp)
	cout=$(mktemp)
	evts_ns1=$(mktemp)
	evts_ns2=$(mktemp)

	trap cleanup EXIT

	make_file "$cin" "client" 1
	make_file "$sin" "server" 1
}

cleanup()
{
	rm -f "$cin" "$cout" "$sinfail"
	rm -f "$sin" "$sout" "$cinsent" "$cinfail"
	rm -rf $evts_ns1 $evts_ns2
	cleanup_partial
}

skip_test()
{
	if [ "${#only_tests_ids[@]}" -eq 0 ] && [ "${#only_tests_names[@]}" -eq 0 ]; then
		return 1
	fi

	local i
	for i in "${only_tests_ids[@]}"; do
		if [ "${TEST_COUNT}" -eq "${i}" ]; then
			return 1
		fi
	done
	for i in "${only_tests_names[@]}"; do
		if [ "${TEST_NAME}" = "${i}" ]; then
			return 1
		fi
	done

	return 0
}

# $1: test name
reset()
{
	TEST_NAME="${1}"

	TEST_COUNT=$((TEST_COUNT+1))

	if skip_test; then
		return 1
	fi

	if [ "${init}" != "1" ]; then
		init
	else
		cleanup_partial
	fi

	init_partial

	return 0
}

# $1: test name
reset_with_cookies()
{
	reset "${1}" || return 1

	local netns
	for netns in "$ns1" "$ns2"; do
		ip netns exec $netns sysctl -q net.ipv4.tcp_syncookies=2
	done
}

# $1: test name
reset_with_add_addr_timeout()
{
	local ip="${2:-4}"
	local tables

	reset "${1}" || return 1

	tables="iptables"
	if [ $ip -eq 6 ]; then
		tables="ip6tables"
	fi

	ip netns exec $ns1 sysctl -q net.mptcp.add_addr_timeout=1
	ip netns exec $ns2 $tables -A OUTPUT -p tcp \
		-m tcp --tcp-option 30 \
		-m bpf --bytecode \
		"$CBPF_MPTCP_SUBOPTION_ADD_ADDR" \
		-j DROP
}

# $1: test name
reset_with_checksum()
{
	local ns1_enable=$1
	local ns2_enable=$2

	reset "checksum test ${1} ${2}" || return 1

	ip netns exec $ns1 sysctl -q net.mptcp.checksum_enabled=$ns1_enable
	ip netns exec $ns2 sysctl -q net.mptcp.checksum_enabled=$ns2_enable

	validate_checksum=1
}

reset_with_allow_join_id0()
{
	local ns1_enable=$2
	local ns2_enable=$3

	reset "${1}" || return 1

	ip netns exec $ns1 sysctl -q net.mptcp.allow_join_initial_addr_port=$ns1_enable
	ip netns exec $ns2 sysctl -q net.mptcp.allow_join_initial_addr_port=$ns2_enable
}

# Modify TCP payload without corrupting the TCP packet
#
# This rule inverts a 8-bit word at byte offset 148 for the 2nd TCP ACK packets
# carrying enough data.
# Once it is done, the TCP Checksum field is updated so the packet is still
# considered as valid at the TCP level.
# Because the MPTCP checksum, covering the TCP options and data, has not been
# updated, the modification will be detected and an MP_FAIL will be emitted:
# what we want to validate here without corrupting "random" MPTCP options.
#
# To avoid having tc producing this pr_info() message for each TCP ACK packets
# not carrying enough data:
#
#     tc action pedit offset 162 out of bounds
#
# Netfilter is used to mark packets with enough data.
reset_with_fail()
{
	reset "${1}" || return 1

	ip netns exec $ns1 sysctl -q net.mptcp.checksum_enabled=1
	ip netns exec $ns2 sysctl -q net.mptcp.checksum_enabled=1

	check_invert=1
	validate_checksum=1
	local i="$2"
	local ip="${3:-4}"
	local tables

	tables="iptables"
	if [ $ip -eq 6 ]; then
		tables="ip6tables"
	fi

	ip netns exec $ns2 $tables \
		-t mangle \
		-A OUTPUT \
		-o ns2eth$i \
		-p tcp \
		-m length --length 150:9999 \
		-m statistic --mode nth --packet 1 --every 99999 \
		-j MARK --set-mark 42 || exit 1

	tc -n $ns2 qdisc add dev ns2eth$i clsact || exit 1
	tc -n $ns2 filter add dev ns2eth$i egress \
		protocol ip prio 1000 \
		handle 42 fw \
		action pedit munge offset 148 u8 invert \
		pipe csum tcp \
		index 100 || exit 1
}

reset_with_events()
{
	reset "${1}" || return 1

	:> "$evts_ns1"
	:> "$evts_ns2"
	ip netns exec $ns1 ./pm_nl_ctl events >> "$evts_ns1" 2>&1 &
	evts_ns1_pid=$!
	ip netns exec $ns2 ./pm_nl_ctl events >> "$evts_ns2" 2>&1 &
	evts_ns2_pid=$!
}

fail_test()
{
	ret=1
	failed_tests[${TEST_COUNT}]="${TEST_NAME}"
}

get_failed_tests_ids()
{
	# sorted
	local i
	for i in "${!failed_tests[@]}"; do
		echo "${i}"
	done | sort -n
}

print_file_err()
{
	ls -l "$1" 1>&2
	echo "Trailing bytes are: "
	tail -c 27 "$1"
}

check_transfer()
{
	local in=$1
	local out=$2
	local what=$3
	local bytes=$4
	local i a b

	local line
	if [ -n "$bytes" ]; then
		# when truncating we must check the size explicitly
		local out_size=$(wc -c $out | awk '{print $1}')
		if [ $out_size -ne $bytes ]; then
			echo "[ FAIL ] $what output file has wrong size ($out_size, $bytes)"
			fail_test
			return 1
		fi
		bytes="--bytes=${bytes}"
	fi
	cmp -l "$in" "$out" ${bytes} | while read -r i a b; do
		local sum=$((0${a} + 0${b}))
		if [ $check_invert -eq 0 ] || [ $sum -ne $((0xff)) ]; then
			echo "[ FAIL ] $what does not match (in, out):"
			print_file_err "$in"
			print_file_err "$out"
			fail_test

			return 1
		else
			echo "$what has inverted byte at ${i}"
		fi
	done

	return 0
}

do_ping()
{
	local listener_ns="$1"
	local connector_ns="$2"
	local connect_addr="$3"

	if ! ip netns exec ${connector_ns} ping -q -c 1 $connect_addr >/dev/null; then
		echo "$listener_ns -> $connect_addr connectivity [ FAIL ]" 1>&2
		fail_test
	fi
}

link_failure()
{
	local ns="$1"

	if [ -z "$FAILING_LINKS" ]; then
		l=$((RANDOM%4))
		FAILING_LINKS=$((l+1))
	fi

	local l
	for l in $FAILING_LINKS; do
		local veth="ns1eth$l"
		ip -net "$ns" link set "$veth" down
	done
}

# $1: IP address
is_v6()
{
	[ -z "${1##*:*}" ]
}

# $1: ns, $2: port
wait_local_port_listen()
{
	local listener_ns="${1}"
	local port="${2}"

	local port_hex
	port_hex="$(printf "%04X" "${port}")"

	local i
	for i in $(seq 10); do
		ip netns exec "${listener_ns}" cat /proc/net/tcp* | \
			awk "BEGIN {rc=1} {if (\$2 ~ /:${port_hex}\$/ && \$4 ~ /0A/) {rc=0; exit}} END {exit rc}" &&
			break
		sleep 0.1
	done
}

rm_addr_count()
{
	local ns=${1}

	ip netns exec ${ns} nstat -as | grep MPTcpExtRmAddr | awk '{print $2}'
}

# $1: ns, $2: old rm_addr counter in $ns
wait_rm_addr()
{
	local ns="${1}"
	local old_cnt="${2}"
	local cnt

	local i
	for i in $(seq 10); do
		cnt=$(rm_addr_count ${ns})
		[ "$cnt" = "${old_cnt}" ] || break
		sleep 0.1
	done
}

wait_mpj()
{
	local ns="${1}"
	local cnt old_cnt

	old_cnt=$(ip netns exec ${ns} nstat -as | grep MPJoinAckRx | awk '{print $2}')

	local i
	for i in $(seq 10); do
		cnt=$(ip netns exec ${ns} nstat -as | grep MPJoinAckRx | awk '{print $2}')
		[ "$cnt" = "${old_cnt}" ] || break
		sleep 0.1
	done
}

kill_wait()
{
	kill $1 > /dev/null 2>&1
	wait $1 2>/dev/null
}

kill_events_pids()
{
	kill_wait $evts_ns1_pid
	kill_wait $evts_ns2_pid
}

kill_tests_wait()
{
	kill -SIGUSR1 $(ip netns pids $ns2) $(ip netns pids $ns1)
	wait
}

pm_nl_set_limits()
{
	local ns=$1
	local addrs=$2
	local subflows=$3

	if [ $ip_mptcp -eq 1 ]; then
		ip -n $ns mptcp limits set add_addr_accepted $addrs subflows $subflows
	else
		ip netns exec $ns ./pm_nl_ctl limits $addrs $subflows
	fi
}

pm_nl_add_endpoint()
{
	local ns=$1
	local addr=$2
	local flags _flags
	local port _port
	local dev _dev
	local id _id
	local nr=2

	local p
	for p in "${@}"
	do
		if [ $p = "flags" ]; then
			eval _flags=\$"$nr"
			[ -n "$_flags" ]; flags="flags $_flags"
		fi
		if [ $p = "dev" ]; then
			eval _dev=\$"$nr"
			[ -n "$_dev" ]; dev="dev $_dev"
		fi
		if [ $p = "id" ]; then
			eval _id=\$"$nr"
			[ -n "$_id" ]; id="id $_id"
		fi
		if [ $p = "port" ]; then
			eval _port=\$"$nr"
			[ -n "$_port" ]; port="port $_port"
		fi

		nr=$((nr + 1))
	done

	if [ $ip_mptcp -eq 1 ]; then
		ip -n $ns mptcp endpoint add $addr ${_flags//","/" "} $dev $id $port
	else
		ip netns exec $ns ./pm_nl_ctl add $addr $flags $dev $id $port
	fi
}

pm_nl_del_endpoint()
{
	local ns=$1
	local id=$2
	local addr=$3

	if [ $ip_mptcp -eq 1 ]; then
		ip -n $ns mptcp endpoint delete id $id $addr
	else
		ip netns exec $ns ./pm_nl_ctl del $id $addr
	fi
}

pm_nl_flush_endpoint()
{
	local ns=$1

	if [ $ip_mptcp -eq 1 ]; then
		ip -n $ns mptcp endpoint flush
	else
		ip netns exec $ns ./pm_nl_ctl flush
	fi
}

pm_nl_show_endpoints()
{
	local ns=$1

	if [ $ip_mptcp -eq 1 ]; then
		ip -n $ns mptcp endpoint show
	else
		ip netns exec $ns ./pm_nl_ctl dump
	fi
}

pm_nl_change_endpoint()
{
	local ns=$1
	local id=$2
	local flags=$3

	if [ $ip_mptcp -eq 1 ]; then
		ip -n $ns mptcp endpoint change id $id ${flags//","/" "}
	else
		ip netns exec $ns ./pm_nl_ctl set id $id flags $flags
	fi
}

pm_nl_check_endpoint()
{
	local line expected_line
	local need_title=$1
	local msg="$2"
	local ns=$3
	local addr=$4
	local _flags=""
	local flags
	local _port
	local port
	local dev
	local _id
	local id

	if [ "${need_title}" = 1 ]; then
		printf "%03u %-36s %s" "${TEST_COUNT}" "${TEST_NAME}" "${msg}"
	else
		printf "%-${nr_blank}s %s" " " "${msg}"
	fi

	shift 4
	while [ -n "$1" ]; do
		if [ $1 = "flags" ]; then
			_flags=$2
			[ -n "$_flags" ]; flags="flags $_flags"
			shift
		elif [ $1 = "dev" ]; then
			[ -n "$2" ]; dev="dev $1"
			shift
		elif [ $1 = "id" ]; then
			_id=$2
			[ -n "$_id" ]; id="id $_id"
			shift
		elif [ $1 = "port" ]; then
			_port=$2
			[ -n "$_port" ]; port=" port $_port"
			shift
		fi

		shift
	done

	if [ -z "$id" ]; then
		echo "[skip] bad test - missing endpoint id"
		return
	fi

	if [ $ip_mptcp -eq 1 ]; then
		line=$(ip -n $ns mptcp endpoint show $id)
		# the dump order is: address id flags port dev
		expected_line="$addr"
		[ -n "$addr" ] && expected_line="$expected_line $addr"
		expected_line="$expected_line $id"
		[ -n "$_flags" ] && expected_line="$expected_line ${_flags//","/" "}"
		[ -n "$dev" ] && expected_line="$expected_line $dev"
		[ -n "$port" ] && expected_line="$expected_line $port"
	else
		line=$(ip netns exec $ns ./pm_nl_ctl get $_id)
		# the dump order is: id flags dev address port
		expected_line="$id"
		[ -n "$flags" ] && expected_line="$expected_line $flags"
		[ -n "$dev" ] && expected_line="$expected_line $dev"
		[ -n "$addr" ] && expected_line="$expected_line $addr"
		[ -n "$_port" ] && expected_line="$expected_line $_port"
	fi
	if [ "$line" = "$expected_line" ]; then
		echo "[ ok ]"
	else
		echo "[fail] expected '$expected_line' found '$line'"
		fail_test
	fi
}

filter_tcp_from()
{
	local ns="${1}"
	local src="${2}"
	local target="${3}"

	ip netns exec "${ns}" iptables -A INPUT -s "${src}" -p tcp -j "${target}"
}

do_transfer()
{
	local listener_ns="$1"
	local connector_ns="$2"
	local cl_proto="$3"
	local srv_proto="$4"
	local connect_addr="$5"
	local test_link_fail="$6"
	local addr_nr_ns1="$7"
	local addr_nr_ns2="$8"
	local speed="$9"
	local sflags="${10}"

	local port=$((10000 + TEST_COUNT - 1))
	local cappid
	local userspace_pm=0

	:> "$cout"
	:> "$sout"
	:> "$capout"

	if [ $capture -eq 1 ]; then
		local capuser
		if [ -z $SUDO_USER ] ; then
			capuser=""
		else
			capuser="-Z $SUDO_USER"
		fi

		capfile=$(printf "mp_join-%02u-%s.pcap" "$TEST_COUNT" "${listener_ns}")

		echo "Capturing traffic for test $TEST_COUNT into $capfile"
		ip netns exec ${listener_ns} tcpdump -i any -s 65535 -B 32768 $capuser -w $capfile > "$capout" 2>&1 &
		cappid=$!

		sleep 1
	fi

	NSTAT_HISTORY=/tmp/${listener_ns}.nstat ip netns exec ${listener_ns} \
		nstat -n
	NSTAT_HISTORY=/tmp/${connector_ns}.nstat ip netns exec ${connector_ns} \
		nstat -n

	local extra_args
	if [ $speed = "fast" ]; then
		extra_args="-j"
	elif [ $speed = "slow" ]; then
		extra_args="-r 50"
	elif [[ $speed = "speed_"* ]]; then
		extra_args="-r ${speed:6}"
	fi

	if [[ "${addr_nr_ns1}" = "userspace_"* ]]; then
		userspace_pm=1
		addr_nr_ns1=${addr_nr_ns1:10}
	fi

	local flags="subflow"
	local extra_cl_args=""
	local extra_srv_args=""
	local trunc_size=""
	if [[ "${addr_nr_ns2}" = "fastclose_"* ]]; then
		if [ ${test_link_fail} -le 1 ]; then
			echo "fastclose tests need test_link_fail argument"
			fail_test
			return 1
		fi

		# disconnect
		trunc_size=${test_link_fail}
		local side=${addr_nr_ns2:10}

		if [ ${side} = "client" ]; then
			extra_cl_args="-f ${test_link_fail}"
			extra_srv_args="-f -1"
		elif [ ${side} = "server" ]; then
			extra_srv_args="-f ${test_link_fail}"
			extra_cl_args="-f -1"
		else
			echo "wrong/unknown fastclose spec ${side}"
			fail_test
			return 1
		fi
		addr_nr_ns2=0
	elif [[ "${addr_nr_ns2}" = "userspace_"* ]]; then
		userspace_pm=1
		addr_nr_ns2=${addr_nr_ns2:10}
	elif [[ "${addr_nr_ns2}" = "fullmesh_"* ]]; then
		flags="${flags},fullmesh"
		addr_nr_ns2=${addr_nr_ns2:9}
	fi

	local local_addr
	if is_v6 "${connect_addr}"; then
		local_addr="::"
	else
		local_addr="0.0.0.0"
	fi

	extra_srv_args="$extra_args $extra_srv_args"
	if [ "$test_link_fail" -gt 1 ];then
		timeout ${timeout_test} \
			ip netns exec ${listener_ns} \
				./mptcp_connect -t ${timeout_poll} -l -p $port -s ${srv_proto} \
					$extra_srv_args ${local_addr} < "$sinfail" > "$sout" &
	else
		timeout ${timeout_test} \
			ip netns exec ${listener_ns} \
				./mptcp_connect -t ${timeout_poll} -l -p $port -s ${srv_proto} \
					$extra_srv_args ${local_addr} < "$sin" > "$sout" &
	fi
	local spid=$!

	wait_local_port_listen "${listener_ns}" "${port}"

	extra_cl_args="$extra_args $extra_cl_args"
	if [ "$test_link_fail" -eq 0 ];then
		timeout ${timeout_test} \
			ip netns exec ${connector_ns} \
				./mptcp_connect -t ${timeout_poll} -p $port -s ${cl_proto} \
					$extra_cl_args $connect_addr < "$cin" > "$cout" &
	elif [ "$test_link_fail" -eq 1 ] || [ "$test_link_fail" -eq 2 ];then
		( cat "$cinfail" ; sleep 2; link_failure $listener_ns ; cat "$cinfail" ) | \
			tee "$cinsent" | \
			timeout ${timeout_test} \
				ip netns exec ${connector_ns} \
					./mptcp_connect -t ${timeout_poll} -p $port -s ${cl_proto} \
						$extra_cl_args $connect_addr > "$cout" &
	else
		tee "$cinsent" < "$cinfail" | \
			timeout ${timeout_test} \
				ip netns exec ${connector_ns} \
					./mptcp_connect -t ${timeout_poll} -p $port -s ${cl_proto} \
						$extra_cl_args $connect_addr > "$cout" &
	fi
	local cpid=$!

	# let the mptcp subflow be established in background before
	# do endpoint manipulation
	if [ $addr_nr_ns1 != "0" ] || [ $addr_nr_ns2 != "0" ]; then
		sleep 1
	fi

	if [ $addr_nr_ns1 -gt 0 ]; then
		local counter=2
		local add_nr_ns1=${addr_nr_ns1}
		local id=10
		local tk
		while [ $add_nr_ns1 -gt 0 ]; do
			local addr
			if is_v6 "${connect_addr}"; then
				addr="dead:beef:$counter::1"
			else
				addr="10.0.$counter.1"
			fi
			if [ $userspace_pm -eq 0 ]; then
				pm_nl_add_endpoint $ns1 $addr flags signal
			else
				tk=$(grep "type:1," "$evts_ns1" |
				     sed -n 's/.*\(token:\)\([[:digit:]]*\).*$/\2/p;q')
				ip netns exec ${listener_ns} ./pm_nl_ctl ann $addr token $tk id $id
				sleep 1
				ip netns exec ${listener_ns} ./pm_nl_ctl rem token $tk id $id
			fi

			counter=$((counter + 1))
			add_nr_ns1=$((add_nr_ns1 - 1))
			id=$((id + 1))
		done
	elif [ $addr_nr_ns1 -lt 0 ]; then
		local rm_nr_ns1=$((-addr_nr_ns1))
		if [ $rm_nr_ns1 -lt 8 ]; then
			local counter=0
			local line
			pm_nl_show_endpoints ${listener_ns} | while read -r line; do
				# shellcheck disable=SC2206 # we do want to split per word
				local arr=($line)
				local nr=0

				local i
				for i in "${arr[@]}"; do
					if [ $i = "id" ]; then
						if [ $counter -eq $rm_nr_ns1 ]; then
							break
						fi
						id=${arr[$nr+1]}
						rm_addr=$(rm_addr_count ${connector_ns})
						pm_nl_del_endpoint ${listener_ns} $id
						wait_rm_addr ${connector_ns} ${rm_addr}
						counter=$((counter + 1))
					fi
					nr=$((nr + 1))
				done
			done
		elif [ $rm_nr_ns1 -eq 8 ]; then
			pm_nl_flush_endpoint ${listener_ns}
		elif [ $rm_nr_ns1 -eq 9 ]; then
			pm_nl_del_endpoint ${listener_ns} 0 ${connect_addr}
		fi
	fi

	# if newly added endpoints must be deleted, give the background msk
	# some time to created them
	[ $addr_nr_ns1 -gt 0 ] && [ $addr_nr_ns2 -lt 0 ] && sleep 1

	if [ $addr_nr_ns2 -gt 0 ]; then
		local add_nr_ns2=${addr_nr_ns2}
		local counter=3
		local id=20
		local tk da dp sp
		while [ $add_nr_ns2 -gt 0 ]; do
			local addr
			if is_v6 "${connect_addr}"; then
				addr="dead:beef:$counter::2"
			else
				addr="10.0.$counter.2"
			fi
			if [ $userspace_pm -eq 0 ]; then
				pm_nl_add_endpoint $ns2 $addr flags $flags
			else
				tk=$(sed -n 's/.*\(token:\)\([[:digit:]]*\).*$/\2/p;q' "$evts_ns2")
				da=$(sed -n 's/.*\(daddr4:\)\([0-9.]*\).*$/\2/p;q' "$evts_ns2")
				dp=$(sed -n 's/.*\(dport:\)\([[:digit:]]*\).*$/\2/p;q' "$evts_ns2")
				ip netns exec ${connector_ns} ./pm_nl_ctl csf lip $addr lid $id \
									rip $da rport $dp token $tk
				sleep 1
				sp=$(grep "type:10" "$evts_ns2" |
				     sed -n 's/.*\(sport:\)\([[:digit:]]*\).*$/\2/p;q')
				ip netns exec ${connector_ns} ./pm_nl_ctl dsf lip $addr lport $sp \
									rip $da rport $dp token $tk
			fi
			counter=$((counter + 1))
			add_nr_ns2=$((add_nr_ns2 - 1))
			id=$((id + 1))
		done
	elif [ $addr_nr_ns2 -lt 0 ]; then
		local rm_nr_ns2=$((-addr_nr_ns2))
		if [ $rm_nr_ns2 -lt 8 ]; then
			local counter=0
			local line
			pm_nl_show_endpoints ${connector_ns} | while read -r line; do
				# shellcheck disable=SC2206 # we do want to split per word
				local arr=($line)
				local nr=0

				local i
				for i in "${arr[@]}"; do
					if [ $i = "id" ]; then
						if [ $counter -eq $rm_nr_ns2 ]; then
							break
						fi
						local id rm_addr
						# rm_addr are serialized, allow the previous one to
						# complete
						id=${arr[$nr+1]}
						rm_addr=$(rm_addr_count ${listener_ns})
						pm_nl_del_endpoint ${connector_ns} $id
						wait_rm_addr ${listener_ns} ${rm_addr}
						counter=$((counter + 1))
					fi
					nr=$((nr + 1))
				done
			done
		elif [ $rm_nr_ns2 -eq 8 ]; then
			pm_nl_flush_endpoint ${connector_ns}
		elif [ $rm_nr_ns2 -eq 9 ]; then
			local addr
			if is_v6 "${connect_addr}"; then
				addr="dead:beef:1::2"
			else
				addr="10.0.1.2"
			fi
			pm_nl_del_endpoint ${connector_ns} 0 $addr
		fi
	fi

	if [ -n "${sflags}" ]; then
		sleep 1

		local netns
		for netns in "$ns1" "$ns2"; do
			local line
			pm_nl_show_endpoints $netns | while read -r line; do
				# shellcheck disable=SC2206 # we do want to split per word
				local arr=($line)
				local nr=0
				local id

				local i
				for i in "${arr[@]}"; do
					if [ $i = "id" ]; then
						id=${arr[$nr+1]}
					fi
					nr=$((nr + 1))
				done
				pm_nl_change_endpoint $netns $id $sflags
			done
		done
	fi

	wait $cpid
	local retc=$?
	wait $spid
	local rets=$?

	if [ $capture -eq 1 ]; then
	    sleep 1
	    kill $cappid
	fi

	NSTAT_HISTORY=/tmp/${listener_ns}.nstat ip netns exec ${listener_ns} \
		nstat | grep Tcp > /tmp/${listener_ns}.out
	NSTAT_HISTORY=/tmp/${connector_ns}.nstat ip netns exec ${connector_ns} \
		nstat | grep Tcp > /tmp/${connector_ns}.out

	if [ ${rets} -ne 0 ] || [ ${retc} -ne 0 ]; then
		echo " client exit code $retc, server $rets" 1>&2
		echo -e "\nnetns ${listener_ns} socket stat for ${port}:" 1>&2
		ip netns exec ${listener_ns} ss -Menita 1>&2 -o "sport = :$port"
		cat /tmp/${listener_ns}.out
		echo -e "\nnetns ${connector_ns} socket stat for ${port}:" 1>&2
		ip netns exec ${connector_ns} ss -Menita 1>&2 -o "dport = :$port"
		cat /tmp/${connector_ns}.out

		cat "$capout"
		fail_test
		return 1
	fi

	if [ "$test_link_fail" -gt 1 ];then
		check_transfer $sinfail $cout "file received by client" $trunc_size
	else
		check_transfer $sin $cout "file received by client" $trunc_size
	fi
	retc=$?
	if [ "$test_link_fail" -eq 0 ];then
		check_transfer $cin $sout "file received by server" $trunc_size
	else
		check_transfer $cinsent $sout "file received by server" $trunc_size
	fi
	rets=$?

	if [ $retc -eq 0 ] && [ $rets -eq 0 ];then
		cat "$capout"
		return 0
	fi

	cat "$capout"
	return 1
}

make_file()
{
	local name=$1
	local who=$2
	local size=$3

	dd if=/dev/urandom of="$name" bs=1024 count=$size 2> /dev/null
	echo -e "\nMPTCP_TEST_FILE_END_MARKER" >> "$name"

	echo "Created $name (size $size KB) containing data sent by $who"
}

run_tests()
{
	local listener_ns="$1"
	local connector_ns="$2"
	local connect_addr="$3"
	local test_linkfail="${4:-0}"
	local addr_nr_ns1="${5:-0}"
	local addr_nr_ns2="${6:-0}"
	local speed="${7:-fast}"
	local sflags="${8:-""}"

	local size

	# The values above 2 are reused to make test files
	# with the given sizes (KB)
	if [ "$test_linkfail" -gt 2 ]; then
		size=$test_linkfail

		if [ -z "$cinfail" ]; then
			cinfail=$(mktemp)
		fi
		make_file "$cinfail" "client" $size
	# create the input file for the failure test when
	# the first failure test run
	elif [ "$test_linkfail" -ne 0 ] && [ -z "$cinfail" ]; then
		# the client file must be considerably larger
		# of the maximum expected cwin value, or the
		# link utilization will be not predicable
		size=$((RANDOM%2))
		size=$((size+1))
		size=$((size*8192))
		size=$((size + ( RANDOM % 8192) ))

		cinfail=$(mktemp)
		make_file "$cinfail" "client" $size
	fi

	if [ "$test_linkfail" -gt 2 ]; then
		size=$test_linkfail

		if [ -z "$sinfail" ]; then
			sinfail=$(mktemp)
		fi
		make_file "$sinfail" "server" $size
	elif [ "$test_linkfail" -eq 2 ] && [ -z "$sinfail" ]; then
		size=$((RANDOM%16))
		size=$((size+1))
		size=$((size*2048))

		sinfail=$(mktemp)
		make_file "$sinfail" "server" $size
	fi

	do_transfer ${listener_ns} ${connector_ns} MPTCP MPTCP ${connect_addr} \
		${test_linkfail} ${addr_nr_ns1} ${addr_nr_ns2} ${speed} ${sflags}
}

dump_stats()
{
	echo Server ns stats
	ip netns exec $ns1 nstat -as | grep Tcp
	echo Client ns stats
	ip netns exec $ns2 nstat -as | grep Tcp
}

chk_csum_nr()
{
	local csum_ns1=${1:-0}
	local csum_ns2=${2:-0}
	local count
	local dump_stats
	local extra_msg=""
	local allow_multi_errors_ns1=0
	local allow_multi_errors_ns2=0

	if [[ "${csum_ns1}" = "+"* ]]; then
		allow_multi_errors_ns1=1
		csum_ns1=${csum_ns1:1}
	fi
	if [[ "${csum_ns2}" = "+"* ]]; then
		allow_multi_errors_ns2=1
		csum_ns2=${csum_ns2:1}
	fi

	printf "%-${nr_blank}s %s" " " "sum"
	count=$(ip netns exec $ns1 nstat -as | grep MPTcpExtDataCsumErr | awk '{print $2}')
	[ -z "$count" ] && count=0
	if [ "$count" != "$csum_ns1" ]; then
		extra_msg="$extra_msg ns1=$count"
	fi
	if { [ "$count" != $csum_ns1 ] && [ $allow_multi_errors_ns1 -eq 0 ]; } ||
	   { [ "$count" -lt $csum_ns1 ] && [ $allow_multi_errors_ns1 -eq 1 ]; }; then
		echo "[fail] got $count data checksum error[s] expected $csum_ns1"
		fail_test
		dump_stats=1
	else
		echo -n "[ ok ]"
	fi
	echo -n " - csum  "
	count=$(ip netns exec $ns2 nstat -as | grep MPTcpExtDataCsumErr | awk '{print $2}')
	[ -z "$count" ] && count=0
	if [ "$count" != "$csum_ns2" ]; then
		extra_msg="$extra_msg ns2=$count"
	fi
	if { [ "$count" != $csum_ns2 ] && [ $allow_multi_errors_ns2 -eq 0 ]; } ||
	   { [ "$count" -lt $csum_ns2 ] && [ $allow_multi_errors_ns2 -eq 1 ]; }; then
		echo "[fail] got $count data checksum error[s] expected $csum_ns2"
		fail_test
		dump_stats=1
	else
		echo -n "[ ok ]"
	fi
	[ "${dump_stats}" = 1 ] && dump_stats

	echo "$extra_msg"
}

chk_fail_nr()
{
	local fail_tx=$1
	local fail_rx=$2
	local ns_invert=${3:-""}
	local count
	local dump_stats
	local ns_tx=$ns1
	local ns_rx=$ns2
	local extra_msg=""
	local allow_tx_lost=0
	local allow_rx_lost=0

	if [[ $ns_invert = "invert" ]]; then
		ns_tx=$ns2
		ns_rx=$ns1
		extra_msg=" invert"
	fi

	if [[ "${fail_tx}" = "-"* ]]; then
		allow_tx_lost=1
		fail_tx=${fail_tx:1}
	fi
	if [[ "${fail_rx}" = "-"* ]]; then
		allow_rx_lost=1
		fail_rx=${fail_rx:1}
	fi

	printf "%-${nr_blank}s %s" " " "ftx"
	count=$(ip netns exec $ns_tx nstat -as | grep MPTcpExtMPFailTx | awk '{print $2}')
	[ -z "$count" ] && count=0
	if [ "$count" != "$fail_tx" ]; then
		extra_msg="$extra_msg,tx=$count"
	fi
	if { [ "$count" != "$fail_tx" ] && [ $allow_tx_lost -eq 0 ]; } ||
	   { [ "$count" -gt "$fail_tx" ] && [ $allow_tx_lost -eq 1 ]; }; then
		echo "[fail] got $count MP_FAIL[s] TX expected $fail_tx"
		fail_test
		dump_stats=1
	else
		echo -n "[ ok ]"
	fi

	echo -n " - failrx"
	count=$(ip netns exec $ns_rx nstat -as | grep MPTcpExtMPFailRx | awk '{print $2}')
	[ -z "$count" ] && count=0
	if [ "$count" != "$fail_rx" ]; then
		extra_msg="$extra_msg,rx=$count"
	fi
	if { [ "$count" != "$fail_rx" ] && [ $allow_rx_lost -eq 0 ]; } ||
	   { [ "$count" -gt "$fail_rx" ] && [ $allow_rx_lost -eq 1 ]; }; then
		echo "[fail] got $count MP_FAIL[s] RX expected $fail_rx"
		fail_test
		dump_stats=1
	else
		echo -n "[ ok ]"
	fi

	[ "${dump_stats}" = 1 ] && dump_stats

	echo "$extra_msg"
}

chk_fclose_nr()
{
	local fclose_tx=$1
	local fclose_rx=$2
	local ns_invert=$3
	local count
	local dump_stats
	local ns_tx=$ns2
	local ns_rx=$ns1
	local extra_msg="   "

	if [[ $ns_invert = "invert" ]]; then
		ns_tx=$ns1
		ns_rx=$ns2
		extra_msg=${extra_msg}"invert"
	fi

	printf "%-${nr_blank}s %s" " " "ctx"
	count=$(ip netns exec $ns_tx nstat -as | grep MPTcpExtMPFastcloseTx | awk '{print $2}')
	[ -z "$count" ] && count=0
	[ "$count" != "$fclose_tx" ] && extra_msg="$extra_msg,tx=$count"
	if [ "$count" != "$fclose_tx" ]; then
		echo "[fail] got $count MP_FASTCLOSE[s] TX expected $fclose_tx"
		fail_test
		dump_stats=1
	else
		echo -n "[ ok ]"
	fi

	echo -n " - fclzrx"
	count=$(ip netns exec $ns_rx nstat -as | grep MPTcpExtMPFastcloseRx | awk '{print $2}')
	[ -z "$count" ] && count=0
	[ "$count" != "$fclose_rx" ] && extra_msg="$extra_msg,rx=$count"
	if [ "$count" != "$fclose_rx" ]; then
		echo "[fail] got $count MP_FASTCLOSE[s] RX expected $fclose_rx"
		fail_test
		dump_stats=1
	else
		echo -n "[ ok ]"
	fi

	[ "${dump_stats}" = 1 ] && dump_stats

	echo "$extra_msg"
}

chk_rst_nr()
{
	local rst_tx=$1
	local rst_rx=$2
	local ns_invert=${3:-""}
	local count
	local dump_stats
	local ns_tx=$ns1
	local ns_rx=$ns2
	local extra_msg=""

	if [[ $ns_invert = "invert" ]]; then
		ns_tx=$ns2
		ns_rx=$ns1
		extra_msg="   invert"
	fi

	printf "%-${nr_blank}s %s" " " "rtx"
	count=$(ip netns exec $ns_tx nstat -as | grep MPTcpExtMPRstTx | awk '{print $2}')
	[ -z "$count" ] && count=0
	if [ $count -lt $rst_tx ]; then
		echo "[fail] got $count MP_RST[s] TX expected $rst_tx"
		fail_test
		dump_stats=1
	else
		echo -n "[ ok ]"
	fi

	echo -n " - rstrx "
	count=$(ip netns exec $ns_rx nstat -as | grep MPTcpExtMPRstRx | awk '{print $2}')
	[ -z "$count" ] && count=0
	if [ "$count" -lt "$rst_rx" ]; then
		echo "[fail] got $count MP_RST[s] RX expected $rst_rx"
		fail_test
		dump_stats=1
	else
		echo -n "[ ok ]"
	fi

	[ "${dump_stats}" = 1 ] && dump_stats

	echo "$extra_msg"
}

chk_infi_nr()
{
	local infi_tx=$1
	local infi_rx=$2
	local count
	local dump_stats

	printf "%-${nr_blank}s %s" " " "itx"
	count=$(ip netns exec $ns2 nstat -as | grep InfiniteMapTx | awk '{print $2}')
	[ -z "$count" ] && count=0
	if [ "$count" != "$infi_tx" ]; then
		echo "[fail] got $count infinite map[s] TX expected $infi_tx"
		fail_test
		dump_stats=1
	else
		echo -n "[ ok ]"
	fi

	echo -n " - infirx"
	count=$(ip netns exec $ns1 nstat -as | grep InfiniteMapRx | awk '{print $2}')
	[ -z "$count" ] && count=0
	if [ "$count" != "$infi_rx" ]; then
		echo "[fail] got $count infinite map[s] RX expected $infi_rx"
		fail_test
		dump_stats=1
	else
		echo "[ ok ]"
	fi

	[ "${dump_stats}" = 1 ] && dump_stats
}

chk_join_nr()
{
	local syn_nr=$1
	local syn_ack_nr=$2
	local ack_nr=$3
	local csum_ns1=${4:-0}
	local csum_ns2=${5:-0}
	local fail_nr=${6:-0}
	local rst_nr=${7:-0}
	local infi_nr=${8:-0}
	local corrupted_pkts=${9:-0}
	local count
	local dump_stats
	local with_cookie
	local title="${TEST_NAME}"

	if [ "${corrupted_pkts}" -gt 0 ]; then
		title+=": ${corrupted_pkts} corrupted pkts"
	fi

	printf "%03u %-36s %s" "${TEST_COUNT}" "${title}" "syn"
	count=$(ip netns exec $ns1 nstat -as | grep MPTcpExtMPJoinSynRx | awk '{print $2}')
	[ -z "$count" ] && count=0
	if [ "$count" != "$syn_nr" ]; then
		echo "[fail] got $count JOIN[s] syn expected $syn_nr"
		fail_test
		dump_stats=1
	else
		echo -n "[ ok ]"
	fi

	echo -n " - synack"
	with_cookie=$(ip netns exec $ns2 sysctl -n net.ipv4.tcp_syncookies)
	count=$(ip netns exec $ns2 nstat -as | grep MPTcpExtMPJoinSynAckRx | awk '{print $2}')
	[ -z "$count" ] && count=0
	if [ "$count" != "$syn_ack_nr" ]; then
		# simult connections exceeding the limit with cookie enabled could go up to
		# synack validation as the conn limit can be enforced reliably only after
		# the subflow creation
		if [ "$with_cookie" = 2 ] && [ "$count" -gt "$syn_ack_nr" ] && [ "$count" -le "$syn_nr" ]; then
			echo -n "[ ok ]"
		else
			echo "[fail] got $count JOIN[s] synack expected $syn_ack_nr"
			fail_test
			dump_stats=1
		fi
	else
		echo -n "[ ok ]"
	fi

	echo -n " - ack"
	count=$(ip netns exec $ns1 nstat -as | grep MPTcpExtMPJoinAckRx | awk '{print $2}')
	[ -z "$count" ] && count=0
	if [ "$count" != "$ack_nr" ]; then
		echo "[fail] got $count JOIN[s] ack expected $ack_nr"
		fail_test
		dump_stats=1
	else
		echo "[ ok ]"
	fi
	[ "${dump_stats}" = 1 ] && dump_stats
	if [ $validate_checksum -eq 1 ]; then
		chk_csum_nr $csum_ns1 $csum_ns2
		chk_fail_nr $fail_nr $fail_nr
		chk_rst_nr $rst_nr $rst_nr
		chk_infi_nr $infi_nr $infi_nr
	fi
}

# a negative value for 'stale_max' means no upper bound:
# for bidirectional transfer, if one peer sleep for a while
# - as these tests do - we can have a quite high number of
# stale/recover conversions, proportional to
# sleep duration/ MPTCP-level RTX interval.
chk_stale_nr()
{
	local ns=$1
	local stale_min=$2
	local stale_max=$3
	local stale_delta=$4
	local dump_stats
	local stale_nr
	local recover_nr

	printf "%-${nr_blank}s %-18s" " " "stale"
	stale_nr=$(ip netns exec $ns nstat -as | grep MPTcpExtSubflowStale | awk '{print $2}')
	[ -z "$stale_nr" ] && stale_nr=0
	recover_nr=$(ip netns exec $ns nstat -as | grep MPTcpExtSubflowRecover | awk '{print $2}')
	[ -z "$recover_nr" ] && recover_nr=0

	if [ $stale_nr -lt $stale_min ] ||
	   { [ $stale_max -gt 0 ] && [ $stale_nr -gt $stale_max ]; } ||
	   [ $((stale_nr - recover_nr)) -ne $stale_delta ]; then
		echo "[fail] got $stale_nr stale[s] $recover_nr recover[s], " \
		     " expected stale in range [$stale_min..$stale_max]," \
		     " stale-recover delta $stale_delta "
		fail_test
		dump_stats=1
	else
		echo "[ ok ]"
	fi

	if [ "${dump_stats}" = 1 ]; then
		echo $ns stats
		ip netns exec $ns ip -s link show
		ip netns exec $ns nstat -as | grep MPTcp
	fi
}

chk_add_nr()
{
	local add_nr=$1
	local echo_nr=$2
	local port_nr=${3:-0}
	local syn_nr=${4:-$port_nr}
	local syn_ack_nr=${5:-$port_nr}
	local ack_nr=${6:-$port_nr}
	local mis_syn_nr=${7:-0}
	local mis_ack_nr=${8:-0}
	local count
	local dump_stats
	local timeout

	timeout=$(ip netns exec $ns1 sysctl -n net.mptcp.add_addr_timeout)

	printf "%-${nr_blank}s %s" " " "add"
	count=$(ip netns exec $ns2 nstat -as MPTcpExtAddAddr | grep MPTcpExtAddAddr | awk '{print $2}')
	[ -z "$count" ] && count=0

	# if the test configured a short timeout tolerate greater then expected
	# add addrs options, due to retransmissions
	if [ "$count" != "$add_nr" ] && { [ "$timeout" -gt 1 ] || [ "$count" -lt "$add_nr" ]; }; then
		echo "[fail] got $count ADD_ADDR[s] expected $add_nr"
		fail_test
		dump_stats=1
	else
		echo -n "[ ok ]"
	fi

	echo -n " - echo  "
	count=$(ip netns exec $ns1 nstat -as | grep MPTcpExtEchoAdd | awk '{print $2}')
	[ -z "$count" ] && count=0
	if [ "$count" != "$echo_nr" ]; then
		echo "[fail] got $count ADD_ADDR echo[s] expected $echo_nr"
		fail_test
		dump_stats=1
	else
		echo -n "[ ok ]"
	fi

	if [ $port_nr -gt 0 ]; then
		echo -n " - pt "
		count=$(ip netns exec $ns2 nstat -as | grep MPTcpExtPortAdd | awk '{print $2}')
		[ -z "$count" ] && count=0
		if [ "$count" != "$port_nr" ]; then
			echo "[fail] got $count ADD_ADDR[s] with a port-number expected $port_nr"
			fail_test
			dump_stats=1
		else
			echo "[ ok ]"
		fi

		printf "%-${nr_blank}s %s" " " "syn"
		count=$(ip netns exec $ns1 nstat -as | grep MPTcpExtMPJoinPortSynRx |
			awk '{print $2}')
		[ -z "$count" ] && count=0
		if [ "$count" != "$syn_nr" ]; then
			echo "[fail] got $count JOIN[s] syn with a different \
				port-number expected $syn_nr"
			fail_test
			dump_stats=1
		else
			echo -n "[ ok ]"
		fi

		echo -n " - synack"
		count=$(ip netns exec $ns2 nstat -as | grep MPTcpExtMPJoinPortSynAckRx |
			awk '{print $2}')
		[ -z "$count" ] && count=0
		if [ "$count" != "$syn_ack_nr" ]; then
			echo "[fail] got $count JOIN[s] synack with a different \
				port-number expected $syn_ack_nr"
			fail_test
			dump_stats=1
		else
			echo -n "[ ok ]"
		fi

		echo -n " - ack"
		count=$(ip netns exec $ns1 nstat -as | grep MPTcpExtMPJoinPortAckRx |
			awk '{print $2}')
		[ -z "$count" ] && count=0
		if [ "$count" != "$ack_nr" ]; then
			echo "[fail] got $count JOIN[s] ack with a different \
				port-number expected $ack_nr"
			fail_test
			dump_stats=1
		else
			echo "[ ok ]"
		fi

		printf "%-${nr_blank}s %s" " " "syn"
		count=$(ip netns exec $ns1 nstat -as | grep MPTcpExtMismatchPortSynRx |
			awk '{print $2}')
		[ -z "$count" ] && count=0
		if [ "$count" != "$mis_syn_nr" ]; then
			echo "[fail] got $count JOIN[s] syn with a mismatched \
				port-number expected $mis_syn_nr"
			fail_test
			dump_stats=1
		else
			echo -n "[ ok ]"
		fi

		echo -n " - ack   "
		count=$(ip netns exec $ns1 nstat -as | grep MPTcpExtMismatchPortAckRx |
			awk '{print $2}')
		[ -z "$count" ] && count=0
		if [ "$count" != "$mis_ack_nr" ]; then
			echo "[fail] got $count JOIN[s] ack with a mismatched \
				port-number expected $mis_ack_nr"
			fail_test
			dump_stats=1
		else
			echo "[ ok ]"
		fi
	else
		echo ""
	fi

	[ "${dump_stats}" = 1 ] && dump_stats
}

chk_rm_nr()
{
	local rm_addr_nr=$1
	local rm_subflow_nr=$2
	local invert
	local simult
	local count
	local dump_stats
	local addr_ns=$ns1
	local subflow_ns=$ns2
	local extra_msg=""

	shift 2
	while [ -n "$1" ]; do
		[ "$1" = "invert" ] && invert=true
		[ "$1" = "simult" ] && simult=true
		shift
	done

	if [ -z $invert ]; then
		addr_ns=$ns1
		subflow_ns=$ns2
	elif [ $invert = "true" ]; then
		addr_ns=$ns2
		subflow_ns=$ns1
		extra_msg="   invert"
	fi

	printf "%-${nr_blank}s %s" " " "rm "
	count=$(ip netns exec $addr_ns nstat -as | grep MPTcpExtRmAddr | awk '{print $2}')
	[ -z "$count" ] && count=0
	if [ "$count" != "$rm_addr_nr" ]; then
		echo "[fail] got $count RM_ADDR[s] expected $rm_addr_nr"
		fail_test
		dump_stats=1
	else
		echo -n "[ ok ]"
	fi

	echo -n " - rmsf  "
	count=$(ip netns exec $subflow_ns nstat -as | grep MPTcpExtRmSubflow | awk '{print $2}')
	[ -z "$count" ] && count=0
	if [ -n "$simult" ]; then
		local cnt suffix

		cnt=$(ip netns exec $addr_ns nstat -as | grep MPTcpExtRmSubflow | awk '{print $2}')

		# in case of simult flush, the subflow removal count on each side is
		# unreliable
		[ -z "$cnt" ] && cnt=0
		count=$((count + cnt))
		[ "$count" != "$rm_subflow_nr" ] && suffix="$count in [$rm_subflow_nr:$((rm_subflow_nr*2))]"
		if [ $count -ge "$rm_subflow_nr" ] && \
		   [ "$count" -le "$((rm_subflow_nr *2 ))" ]; then
			echo "[ ok ] $suffix"
		else
			echo "[fail] got $count RM_SUBFLOW[s] expected in range [$rm_subflow_nr:$((rm_subflow_nr*2))]"
			fail_test
			dump_stats=1
		fi
		return
	fi
	if [ "$count" != "$rm_subflow_nr" ]; then
		echo "[fail] got $count RM_SUBFLOW[s] expected $rm_subflow_nr"
		fail_test
		dump_stats=1
	else
		echo -n "[ ok ]"
	fi

	[ "${dump_stats}" = 1 ] && dump_stats

	echo "$extra_msg"
}

chk_prio_nr()
{
	local mp_prio_nr_tx=$1
	local mp_prio_nr_rx=$2
	local count
	local dump_stats

	printf "%-${nr_blank}s %s" " " "ptx"
	count=$(ip netns exec $ns1 nstat -as | grep MPTcpExtMPPrioTx | awk '{print $2}')
	[ -z "$count" ] && count=0
	if [ "$count" != "$mp_prio_nr_tx" ]; then
		echo "[fail] got $count MP_PRIO[s] TX expected $mp_prio_nr_tx"
		fail_test
		dump_stats=1
	else
		echo -n "[ ok ]"
	fi

	echo -n " - prx   "
	count=$(ip netns exec $ns1 nstat -as | grep MPTcpExtMPPrioRx | awk '{print $2}')
	[ -z "$count" ] && count=0
	if [ "$count" != "$mp_prio_nr_rx" ]; then
		echo "[fail] got $count MP_PRIO[s] RX expected $mp_prio_nr_rx"
		fail_test
		dump_stats=1
	else
		echo "[ ok ]"
	fi

	[ "${dump_stats}" = 1 ] && dump_stats
}

chk_subflow_nr()
{
	local need_title="$1"
	local msg="$2"
	local subflow_nr=$3
	local cnt1
	local cnt2
	local dump_stats

	if [ -n "${need_title}" ]; then
		printf "%03u %-36s %s" "${TEST_COUNT}" "${TEST_NAME}" "${msg}"
	else
		printf "%-${nr_blank}s %s" " " "${msg}"
	fi

	cnt1=$(ss -N $ns1 -tOni | grep -c token)
	cnt2=$(ss -N $ns2 -tOni | grep -c token)
	if [ "$cnt1" != "$subflow_nr" -o "$cnt2" != "$subflow_nr" ]; then
		echo "[fail] got $cnt1:$cnt2 subflows expected $subflow_nr"
		fail_test
		dump_stats=1
	else
		echo "[ ok ]"
	fi

	if [ "${dump_stats}" = 1 ]; then
		ss -N $ns1 -tOni
		ss -N $ns1 -tOni | grep token
		ip -n $ns1 mptcp endpoint
		dump_stats
	fi
}

chk_link_usage()
{
	local ns=$1
	local link=$2
	local out=$3
	local expected_rate=$4

	local tx_link tx_total
	tx_link=$(ip netns exec $ns cat /sys/class/net/$link/statistics/tx_bytes)
	tx_total=$(stat --format=%s $out)
	local tx_rate=$((tx_link * 100 / tx_total))
	local tolerance=5

	printf "%-${nr_blank}s %-18s" " " "link usage"
	if [ $tx_rate -lt $((expected_rate - tolerance)) ] || \
	   [ $tx_rate -gt $((expected_rate + tolerance)) ]; then
		echo "[fail] got $tx_rate% usage, expected $expected_rate%"
		fail_test
	else
		echo "[ ok ]"
	fi
}

wait_attempt_fail()
{
	local timeout_ms=$((timeout_poll * 1000))
	local time=0
	local ns=$1

	while [ $time -lt $timeout_ms ]; do
		local cnt

		cnt=$(ip netns exec $ns nstat -as TcpAttemptFails | grep TcpAttemptFails | awk '{print $2}')

		[ "$cnt" = 1 ] && return 1
		time=$((time + 100))
		sleep 0.1
	done
	return 1
}

set_userspace_pm()
{
	local ns=$1

	ip netns exec $ns sysctl -q net.mptcp.pm_type=1
}

subflows_tests()
{
	if reset "no JOIN"; then
		run_tests $ns1 $ns2 10.0.1.1
		chk_join_nr 0 0 0
	fi

	# subflow limited by client
	if reset "single subflow, limited by client"; then
		pm_nl_set_limits $ns1 0 0
		pm_nl_set_limits $ns2 0 0
		pm_nl_add_endpoint $ns2 10.0.3.2 flags subflow
		run_tests $ns1 $ns2 10.0.1.1
		chk_join_nr 0 0 0
	fi

	# subflow limited by server
	if reset "single subflow, limited by server"; then
		pm_nl_set_limits $ns1 0 0
		pm_nl_set_limits $ns2 0 1
		pm_nl_add_endpoint $ns2 10.0.3.2 flags subflow
		run_tests $ns1 $ns2 10.0.1.1
		chk_join_nr 1 1 0
	fi

	# subflow
	if reset "single subflow"; then
		pm_nl_set_limits $ns1 0 1
		pm_nl_set_limits $ns2 0 1
		pm_nl_add_endpoint $ns2 10.0.3.2 flags subflow
		run_tests $ns1 $ns2 10.0.1.1
		chk_join_nr 1 1 1
	fi

	# multiple subflows
	if reset "multiple subflows"; then
		pm_nl_set_limits $ns1 0 2
		pm_nl_set_limits $ns2 0 2
		pm_nl_add_endpoint $ns2 10.0.3.2 flags subflow
		pm_nl_add_endpoint $ns2 10.0.2.2 flags subflow
		run_tests $ns1 $ns2 10.0.1.1
		chk_join_nr 2 2 2
	fi

	# multiple subflows limited by server
	if reset "multiple subflows, limited by server"; then
		pm_nl_set_limits $ns1 0 1
		pm_nl_set_limits $ns2 0 2
		pm_nl_add_endpoint $ns2 10.0.3.2 flags subflow
		pm_nl_add_endpoint $ns2 10.0.2.2 flags subflow
		run_tests $ns1 $ns2 10.0.1.1
		chk_join_nr 2 2 1
	fi

	# single subflow, dev
	if reset "single subflow, dev"; then
		pm_nl_set_limits $ns1 0 1
		pm_nl_set_limits $ns2 0 1
		pm_nl_add_endpoint $ns2 10.0.3.2 flags subflow dev ns2eth3
		run_tests $ns1 $ns2 10.0.1.1
		chk_join_nr 1 1 1
	fi
}

subflows_error_tests()
{
	# If a single subflow is configured, and matches the MPC src
	# address, no additional subflow should be created
	if reset "no MPC reuse with single endpoint"; then
		pm_nl_set_limits $ns1 0 1
		pm_nl_set_limits $ns2 0 1
		pm_nl_add_endpoint $ns2 10.0.1.2 flags subflow
		run_tests $ns1 $ns2 10.0.1.1 0 0 0 slow
		chk_join_nr 0 0 0
	fi

	# multiple subflows, with subflow creation error
	if reset "multi subflows, with failing subflow"; then
		pm_nl_set_limits $ns1 0 2
		pm_nl_set_limits $ns2 0 2
		pm_nl_add_endpoint $ns2 10.0.3.2 flags subflow
		pm_nl_add_endpoint $ns2 10.0.2.2 flags subflow
		filter_tcp_from $ns1 10.0.3.2 REJECT
		run_tests $ns1 $ns2 10.0.1.1 0 0 0 slow
		chk_join_nr 1 1 1
	fi

	# multiple subflows, with subflow timeout on MPJ
	if reset "multi subflows, with subflow timeout"; then
		pm_nl_set_limits $ns1 0 2
		pm_nl_set_limits $ns2 0 2
		pm_nl_add_endpoint $ns2 10.0.3.2 flags subflow
		pm_nl_add_endpoint $ns2 10.0.2.2 flags subflow
		filter_tcp_from $ns1 10.0.3.2 DROP
		run_tests $ns1 $ns2 10.0.1.1 0 0 0 slow
		chk_join_nr 1 1 1
	fi

	# multiple subflows, check that the endpoint corresponding to
	# closed subflow (due to reset) is not reused if additional
	# subflows are added later
	if reset "multi subflows, fair usage on close"; then
		pm_nl_set_limits $ns1 0 1
		pm_nl_set_limits $ns2 0 1
		pm_nl_add_endpoint $ns2 10.0.3.2 flags subflow
		filter_tcp_from $ns1 10.0.3.2 REJECT
		run_tests $ns1 $ns2 10.0.1.1 0 0 0 slow &

		# mpj subflow will be in TW after the reset
		wait_attempt_fail $ns2
		pm_nl_add_endpoint $ns2 10.0.2.2 flags subflow
		wait

		# additional subflow could be created only if the PM select
		# the later endpoint, skipping the already used one
		chk_join_nr 1 1 1
	fi
}

signal_address_tests()
{
	# add_address, unused
	if reset "unused signal address"; then
		pm_nl_add_endpoint $ns1 10.0.2.1 flags signal
		run_tests $ns1 $ns2 10.0.1.1
		chk_join_nr 0 0 0
		chk_add_nr 1 1
	fi

	# accept and use add_addr
	if reset "signal address"; then
		pm_nl_set_limits $ns1 0 1
		pm_nl_set_limits $ns2 1 1
		pm_nl_add_endpoint $ns1 10.0.2.1 flags signal
		run_tests $ns1 $ns2 10.0.1.1
		chk_join_nr 1 1 1
		chk_add_nr 1 1
	fi

	# accept and use add_addr with an additional subflow
	# note: signal address in server ns and local addresses in client ns must
	# belong to different subnets or one of the listed local address could be
	# used for 'add_addr' subflow
	if reset "subflow and signal"; then
		pm_nl_add_endpoint $ns1 10.0.2.1 flags signal
		pm_nl_set_limits $ns1 0 2
		pm_nl_set_limits $ns2 1 2
		pm_nl_add_endpoint $ns2 10.0.3.2 flags subflow
		run_tests $ns1 $ns2 10.0.1.1
		chk_join_nr 2 2 2
		chk_add_nr 1 1
	fi

	# accept and use add_addr with additional subflows
	if reset "multiple subflows and signal"; then
		pm_nl_set_limits $ns1 0 3
		pm_nl_add_endpoint $ns1 10.0.2.1 flags signal
		pm_nl_set_limits $ns2 1 3
		pm_nl_add_endpoint $ns2 10.0.3.2 flags subflow
		pm_nl_add_endpoint $ns2 10.0.4.2 flags subflow
		run_tests $ns1 $ns2 10.0.1.1
		chk_join_nr 3 3 3
		chk_add_nr 1 1
	fi

	# signal addresses
	if reset "signal addresses"; then
		pm_nl_set_limits $ns1 3 3
		pm_nl_add_endpoint $ns1 10.0.2.1 flags signal
		pm_nl_add_endpoint $ns1 10.0.3.1 flags signal
		pm_nl_add_endpoint $ns1 10.0.4.1 flags signal
		pm_nl_set_limits $ns2 3 3
		run_tests $ns1 $ns2 10.0.1.1
		chk_join_nr 3 3 3
		chk_add_nr 3 3
	fi

	# signal invalid addresses
	if reset "signal invalid addresses"; then
		pm_nl_set_limits $ns1 3 3
		pm_nl_add_endpoint $ns1 10.0.12.1 flags signal
		pm_nl_add_endpoint $ns1 10.0.3.1 flags signal
		pm_nl_add_endpoint $ns1 10.0.14.1 flags signal
		pm_nl_set_limits $ns2 3 3
		run_tests $ns1 $ns2 10.0.1.1
		chk_join_nr 1 1 1
		chk_add_nr 3 3
	fi

	# signal addresses race test
	if reset "signal addresses race test"; then
		pm_nl_set_limits $ns1 4 4
		pm_nl_set_limits $ns2 4 4
		pm_nl_add_endpoint $ns1 10.0.1.1 flags signal
		pm_nl_add_endpoint $ns1 10.0.2.1 flags signal
		pm_nl_add_endpoint $ns1 10.0.3.1 flags signal
		pm_nl_add_endpoint $ns1 10.0.4.1 flags signal
		pm_nl_add_endpoint $ns2 10.0.1.2 flags signal
		pm_nl_add_endpoint $ns2 10.0.2.2 flags signal
		pm_nl_add_endpoint $ns2 10.0.3.2 flags signal
		pm_nl_add_endpoint $ns2 10.0.4.2 flags signal

		# the peer could possibly miss some addr notification, allow retransmission
		ip netns exec $ns1 sysctl -q net.mptcp.add_addr_timeout=1
		run_tests $ns1 $ns2 10.0.1.1 0 0 0 slow
		chk_join_nr 3 3 3

		# the server will not signal the address terminating
		# the MPC subflow
		chk_add_nr 3 3
	fi
}

link_failure_tests()
{
	# accept and use add_addr with additional subflows and link loss
	if reset "multiple flows, signal, link failure"; then
		# without any b/w limit each veth could spool the packets and get
		# them acked at xmit time, so that the corresponding subflow will
		# have almost always no outstanding pkts, the scheduler will pick
		# always the first subflow and we will have hard time testing
		# active backup and link switch-over.
		# Let's set some arbitrary (low) virtual link limits.
		init_shapers
		pm_nl_set_limits $ns1 0 3
		pm_nl_add_endpoint $ns1 10.0.2.1 dev ns1eth2 flags signal
		pm_nl_set_limits $ns2 1 3
		pm_nl_add_endpoint $ns2 10.0.3.2 dev ns2eth3 flags subflow
		pm_nl_add_endpoint $ns2 10.0.4.2 dev ns2eth4 flags subflow
		run_tests $ns1 $ns2 10.0.1.1 1
		chk_join_nr 3 3 3
		chk_add_nr 1 1
		chk_stale_nr $ns2 1 5 1
	fi

	# accept and use add_addr with additional subflows and link loss
	# for bidirectional transfer
	if reset "multi flows, signal, bidi, link fail"; then
		init_shapers
		pm_nl_set_limits $ns1 0 3
		pm_nl_add_endpoint $ns1 10.0.2.1 dev ns1eth2 flags signal
		pm_nl_set_limits $ns2 1 3
		pm_nl_add_endpoint $ns2 10.0.3.2 dev ns2eth3 flags subflow
		pm_nl_add_endpoint $ns2 10.0.4.2 dev ns2eth4 flags subflow
		run_tests $ns1 $ns2 10.0.1.1 2
		chk_join_nr 3 3 3
		chk_add_nr 1 1
		chk_stale_nr $ns2 1 -1 1
	fi

	# 2 subflows plus 1 backup subflow with a lossy link, backup
	# will never be used
	if reset "backup subflow unused, link failure"; then
		init_shapers
		pm_nl_set_limits $ns1 0 2
		pm_nl_add_endpoint $ns1 10.0.2.1 dev ns1eth2 flags signal
		pm_nl_set_limits $ns2 1 2
		FAILING_LINKS="1"
		pm_nl_add_endpoint $ns2 10.0.3.2 dev ns2eth3 flags subflow,backup
		run_tests $ns1 $ns2 10.0.1.1 1
		chk_join_nr 2 2 2
		chk_add_nr 1 1
		chk_link_usage $ns2 ns2eth3 $cinsent 0
	fi

	# 2 lossy links after half transfer, backup will get half of
	# the traffic
	if reset "backup flow used, multi links fail"; then
		init_shapers
		pm_nl_set_limits $ns1 0 2
		pm_nl_add_endpoint $ns1 10.0.2.1 dev ns1eth2 flags signal
		pm_nl_set_limits $ns2 1 2
		pm_nl_add_endpoint $ns2 10.0.3.2 dev ns2eth3 flags subflow,backup
		FAILING_LINKS="1 2"
		run_tests $ns1 $ns2 10.0.1.1 1
		chk_join_nr 2 2 2
		chk_add_nr 1 1
		chk_stale_nr $ns2 2 4 2
		chk_link_usage $ns2 ns2eth3 $cinsent 50
	fi

	# use a backup subflow with the first subflow on a lossy link
	# for bidirectional transfer
	if reset "backup flow used, bidi, link failure"; then
		init_shapers
		pm_nl_set_limits $ns1 0 2
		pm_nl_add_endpoint $ns1 10.0.2.1 dev ns1eth2 flags signal
		pm_nl_set_limits $ns2 1 3
		pm_nl_add_endpoint $ns2 10.0.3.2 dev ns2eth3 flags subflow,backup
		FAILING_LINKS="1 2"
		run_tests $ns1 $ns2 10.0.1.1 2
		chk_join_nr 2 2 2
		chk_add_nr 1 1
		chk_stale_nr $ns2 1 -1 2
		chk_link_usage $ns2 ns2eth3 $cinsent 50
	fi
}

add_addr_timeout_tests()
{
	# add_addr timeout
	if reset_with_add_addr_timeout "signal address, ADD_ADDR timeout"; then
		pm_nl_set_limits $ns1 0 1
		pm_nl_set_limits $ns2 1 1
		pm_nl_add_endpoint $ns1 10.0.2.1 flags signal
		run_tests $ns1 $ns2 10.0.1.1 0 0 0 slow
		chk_join_nr 1 1 1
		chk_add_nr 4 0
	fi

	# add_addr timeout IPv6
	if reset_with_add_addr_timeout "signal address, ADD_ADDR6 timeout" 6; then
		pm_nl_set_limits $ns1 0 1
		pm_nl_set_limits $ns2 1 1
		pm_nl_add_endpoint $ns1 dead:beef:2::1 flags signal
		run_tests $ns1 $ns2 dead:beef:1::1 0 0 0 slow
		chk_join_nr 1 1 1
		chk_add_nr 4 0
	fi

	# signal addresses timeout
	if reset_with_add_addr_timeout "signal addresses, ADD_ADDR timeout"; then
		pm_nl_set_limits $ns1 2 2
		pm_nl_add_endpoint $ns1 10.0.2.1 flags signal
		pm_nl_add_endpoint $ns1 10.0.3.1 flags signal
		pm_nl_set_limits $ns2 2 2
		run_tests $ns1 $ns2 10.0.1.1 0 0 0 speed_10
		chk_join_nr 2 2 2
		chk_add_nr 8 0
	fi

	# signal invalid addresses timeout
	if reset_with_add_addr_timeout "invalid address, ADD_ADDR timeout"; then
		pm_nl_set_limits $ns1 2 2
		pm_nl_add_endpoint $ns1 10.0.12.1 flags signal
		pm_nl_add_endpoint $ns1 10.0.3.1 flags signal
		pm_nl_set_limits $ns2 2 2
		run_tests $ns1 $ns2 10.0.1.1 0 0 0 speed_10
		chk_join_nr 1 1 1
		chk_add_nr 8 0
	fi
}

remove_tests()
{
	# single subflow, remove
	if reset "remove single subflow"; then
		pm_nl_set_limits $ns1 0 1
		pm_nl_set_limits $ns2 0 1
		pm_nl_add_endpoint $ns2 10.0.3.2 flags subflow
		run_tests $ns1 $ns2 10.0.1.1 0 0 -1 slow
		chk_join_nr 1 1 1
		chk_rm_nr 1 1
	fi

	# multiple subflows, remove
	if reset "remove multiple subflows"; then
		pm_nl_set_limits $ns1 0 2
		pm_nl_set_limits $ns2 0 2
		pm_nl_add_endpoint $ns2 10.0.2.2 flags subflow
		pm_nl_add_endpoint $ns2 10.0.3.2 flags subflow
		run_tests $ns1 $ns2 10.0.1.1 0 0 -2 slow
		chk_join_nr 2 2 2
		chk_rm_nr 2 2
	fi

	# single address, remove
	if reset "remove single address"; then
		pm_nl_set_limits $ns1 0 1
		pm_nl_add_endpoint $ns1 10.0.2.1 flags signal
		pm_nl_set_limits $ns2 1 1
		run_tests $ns1 $ns2 10.0.1.1 0 -1 0 slow
		chk_join_nr 1 1 1
		chk_add_nr 1 1
		chk_rm_nr 1 1 invert
	fi

	# subflow and signal, remove
	if reset "remove subflow and signal"; then
		pm_nl_set_limits $ns1 0 2
		pm_nl_add_endpoint $ns1 10.0.2.1 flags signal
		pm_nl_set_limits $ns2 1 2
		pm_nl_add_endpoint $ns2 10.0.3.2 flags subflow
		run_tests $ns1 $ns2 10.0.1.1 0 -1 -1 slow
		chk_join_nr 2 2 2
		chk_add_nr 1 1
		chk_rm_nr 1 1
	fi

	# subflows and signal, remove
	if reset "remove subflows and signal"; then
		pm_nl_set_limits $ns1 0 3
		pm_nl_add_endpoint $ns1 10.0.2.1 flags signal
		pm_nl_set_limits $ns2 1 3
		pm_nl_add_endpoint $ns2 10.0.3.2 flags subflow
		pm_nl_add_endpoint $ns2 10.0.4.2 flags subflow
		run_tests $ns1 $ns2 10.0.1.1 0 -1 -2 speed_10
		chk_join_nr 3 3 3
		chk_add_nr 1 1
		chk_rm_nr 2 2
	fi

	# addresses remove
	if reset "remove addresses"; then
		pm_nl_set_limits $ns1 3 3
		pm_nl_add_endpoint $ns1 10.0.2.1 flags signal id 250
		pm_nl_add_endpoint $ns1 10.0.3.1 flags signal
		pm_nl_add_endpoint $ns1 10.0.4.1 flags signal
		pm_nl_set_limits $ns2 3 3
		run_tests $ns1 $ns2 10.0.1.1 0 -3 0 speed_10
		chk_join_nr 3 3 3
		chk_add_nr 3 3
		chk_rm_nr 3 3 invert
	fi

	# invalid addresses remove
	if reset "remove invalid addresses"; then
		pm_nl_set_limits $ns1 3 3
		pm_nl_add_endpoint $ns1 10.0.12.1 flags signal
		pm_nl_add_endpoint $ns1 10.0.3.1 flags signal
		pm_nl_add_endpoint $ns1 10.0.14.1 flags signal
		pm_nl_set_limits $ns2 3 3
		run_tests $ns1 $ns2 10.0.1.1 0 -3 0 speed_10
		chk_join_nr 1 1 1
		chk_add_nr 3 3
		chk_rm_nr 3 1 invert
	fi

	# subflows and signal, flush
	if reset "flush subflows and signal"; then
		pm_nl_set_limits $ns1 0 3
		pm_nl_add_endpoint $ns1 10.0.2.1 flags signal
		pm_nl_set_limits $ns2 1 3
		pm_nl_add_endpoint $ns2 10.0.3.2 flags subflow
		pm_nl_add_endpoint $ns2 10.0.4.2 flags subflow
		run_tests $ns1 $ns2 10.0.1.1 0 -8 -8 slow
		chk_join_nr 3 3 3
		chk_add_nr 1 1
		chk_rm_nr 1 3 invert simult
	fi

	# subflows flush
	if reset "flush subflows"; then
		pm_nl_set_limits $ns1 3 3
		pm_nl_set_limits $ns2 3 3
		pm_nl_add_endpoint $ns2 10.0.2.2 flags subflow id 150
		pm_nl_add_endpoint $ns2 10.0.3.2 flags subflow
		pm_nl_add_endpoint $ns2 10.0.4.2 flags subflow
		run_tests $ns1 $ns2 10.0.1.1 0 -8 -8 slow
		chk_join_nr 3 3 3
		chk_rm_nr 0 3 simult
	fi

	# addresses flush
	if reset "flush addresses"; then
		pm_nl_set_limits $ns1 3 3
		pm_nl_add_endpoint $ns1 10.0.2.1 flags signal id 250
		pm_nl_add_endpoint $ns1 10.0.3.1 flags signal
		pm_nl_add_endpoint $ns1 10.0.4.1 flags signal
		pm_nl_set_limits $ns2 3 3
		run_tests $ns1 $ns2 10.0.1.1 0 -8 -8 slow
		chk_join_nr 3 3 3
		chk_add_nr 3 3
		chk_rm_nr 3 3 invert simult
	fi

	# invalid addresses flush
	if reset "flush invalid addresses"; then
		pm_nl_set_limits $ns1 3 3
		pm_nl_add_endpoint $ns1 10.0.12.1 flags signal
		pm_nl_add_endpoint $ns1 10.0.3.1 flags signal
		pm_nl_add_endpoint $ns1 10.0.14.1 flags signal
		pm_nl_set_limits $ns2 3 3
		run_tests $ns1 $ns2 10.0.1.1 0 -8 0 slow
		chk_join_nr 1 1 1
		chk_add_nr 3 3
		chk_rm_nr 3 1 invert
	fi

	# remove id 0 subflow
	if reset "remove id 0 subflow"; then
		pm_nl_set_limits $ns1 0 1
		pm_nl_set_limits $ns2 0 1
		pm_nl_add_endpoint $ns2 10.0.3.2 flags subflow
		run_tests $ns1 $ns2 10.0.1.1 0 0 -9 slow
		chk_join_nr 1 1 1
		chk_rm_nr 1 1
	fi

	# remove id 0 address
	if reset "remove id 0 address"; then
		pm_nl_set_limits $ns1 0 1
		pm_nl_add_endpoint $ns1 10.0.2.1 flags signal
		pm_nl_set_limits $ns2 1 1
		run_tests $ns1 $ns2 10.0.1.1 0 -9 0 slow
		chk_join_nr 1 1 1
		chk_add_nr 1 1
		chk_rm_nr 1 1 invert
	fi
}

add_tests()
{
	# add single subflow
	if reset "add single subflow"; then
		pm_nl_set_limits $ns1 0 1
		pm_nl_set_limits $ns2 0 1
		run_tests $ns1 $ns2 10.0.1.1 0 0 1 slow
		chk_join_nr 1 1 1
	fi

	# add signal address
	if reset "add signal address"; then
		pm_nl_set_limits $ns1 0 1
		pm_nl_set_limits $ns2 1 1
		run_tests $ns1 $ns2 10.0.1.1 0 1 0 slow
		chk_join_nr 1 1 1
		chk_add_nr 1 1
	fi

	# add multiple subflows
	if reset "add multiple subflows"; then
		pm_nl_set_limits $ns1 0 2
		pm_nl_set_limits $ns2 0 2
		run_tests $ns1 $ns2 10.0.1.1 0 0 2 slow
		chk_join_nr 2 2 2
	fi

	# add multiple subflows IPv6
	if reset "add multiple subflows IPv6"; then
		pm_nl_set_limits $ns1 0 2
		pm_nl_set_limits $ns2 0 2
		run_tests $ns1 $ns2 dead:beef:1::1 0 0 2 slow
		chk_join_nr 2 2 2
	fi

	# add multiple addresses IPv6
	if reset "add multiple addresses IPv6"; then
		pm_nl_set_limits $ns1 0 2
		pm_nl_set_limits $ns2 2 2
		run_tests $ns1 $ns2 dead:beef:1::1 0 2 0 slow
		chk_join_nr 2 2 2
		chk_add_nr 2 2
	fi
}

ipv6_tests()
{
	# subflow IPv6
	if reset "single subflow IPv6"; then
		pm_nl_set_limits $ns1 0 1
		pm_nl_set_limits $ns2 0 1
		pm_nl_add_endpoint $ns2 dead:beef:3::2 dev ns2eth3 flags subflow
		run_tests $ns1 $ns2 dead:beef:1::1 0 0 0 slow
		chk_join_nr 1 1 1
	fi

	# add_address, unused IPv6
	if reset "unused signal address IPv6"; then
		pm_nl_add_endpoint $ns1 dead:beef:2::1 flags signal
		run_tests $ns1 $ns2 dead:beef:1::1 0 0 0 slow
		chk_join_nr 0 0 0
		chk_add_nr 1 1
	fi

	# signal address IPv6
	if reset "single address IPv6"; then
		pm_nl_set_limits $ns1 0 1
		pm_nl_add_endpoint $ns1 dead:beef:2::1 flags signal
		pm_nl_set_limits $ns2 1 1
		run_tests $ns1 $ns2 dead:beef:1::1 0 0 0 slow
		chk_join_nr 1 1 1
		chk_add_nr 1 1
	fi

	# single address IPv6, remove
	if reset "remove single address IPv6"; then
		pm_nl_set_limits $ns1 0 1
		pm_nl_add_endpoint $ns1 dead:beef:2::1 flags signal
		pm_nl_set_limits $ns2 1 1
		run_tests $ns1 $ns2 dead:beef:1::1 0 -1 0 slow
		chk_join_nr 1 1 1
		chk_add_nr 1 1
		chk_rm_nr 1 1 invert
	fi

	# subflow and signal IPv6, remove
	if reset "remove subflow and signal IPv6"; then
		pm_nl_set_limits $ns1 0 2
		pm_nl_add_endpoint $ns1 dead:beef:2::1 flags signal
		pm_nl_set_limits $ns2 1 2
		pm_nl_add_endpoint $ns2 dead:beef:3::2 dev ns2eth3 flags subflow
		run_tests $ns1 $ns2 dead:beef:1::1 0 -1 -1 slow
		chk_join_nr 2 2 2
		chk_add_nr 1 1
		chk_rm_nr 1 1
	fi
}

v4mapped_tests()
{
	# subflow IPv4-mapped to IPv4-mapped
	if reset "single subflow IPv4-mapped"; then
		pm_nl_set_limits $ns1 0 1
		pm_nl_set_limits $ns2 0 1
		pm_nl_add_endpoint $ns2 "::ffff:10.0.3.2" flags subflow
		run_tests $ns1 $ns2 "::ffff:10.0.1.1"
		chk_join_nr 1 1 1
	fi

	# signal address IPv4-mapped with IPv4-mapped sk
	if reset "signal address IPv4-mapped"; then
		pm_nl_set_limits $ns1 0 1
		pm_nl_set_limits $ns2 1 1
		pm_nl_add_endpoint $ns1 "::ffff:10.0.2.1" flags signal
		run_tests $ns1 $ns2 "::ffff:10.0.1.1"
		chk_join_nr 1 1 1
		chk_add_nr 1 1
	fi

	# subflow v4-map-v6
	if reset "single subflow v4-map-v6"; then
		pm_nl_set_limits $ns1 0 1
		pm_nl_set_limits $ns2 0 1
		pm_nl_add_endpoint $ns2 10.0.3.2 flags subflow
		run_tests $ns1 $ns2 "::ffff:10.0.1.1"
		chk_join_nr 1 1 1
	fi

	# signal address v4-map-v6
	if reset "signal address v4-map-v6"; then
		pm_nl_set_limits $ns1 0 1
		pm_nl_set_limits $ns2 1 1
		pm_nl_add_endpoint $ns1 10.0.2.1 flags signal
		run_tests $ns1 $ns2 "::ffff:10.0.1.1"
		chk_join_nr 1 1 1
		chk_add_nr 1 1
	fi

	# subflow v6-map-v4
	if reset "single subflow v6-map-v4"; then
		pm_nl_set_limits $ns1 0 1
		pm_nl_set_limits $ns2 0 1
		pm_nl_add_endpoint $ns2 "::ffff:10.0.3.2" flags subflow
		run_tests $ns1 $ns2 10.0.1.1
		chk_join_nr 1 1 1
	fi

	# signal address v6-map-v4
	if reset "signal address v6-map-v4"; then
		pm_nl_set_limits $ns1 0 1
		pm_nl_set_limits $ns2 1 1
		pm_nl_add_endpoint $ns1 "::ffff:10.0.2.1" flags signal
		run_tests $ns1 $ns2 10.0.1.1
		chk_join_nr 1 1 1
		chk_add_nr 1 1
	fi

	# no subflow IPv6 to v4 address
	if reset "no JOIN with diff families v4-v6"; then
		pm_nl_set_limits $ns1 0 1
		pm_nl_set_limits $ns2 0 1
		pm_nl_add_endpoint $ns2 dead:beef:2::2 flags subflow
		run_tests $ns1 $ns2 10.0.1.1
		chk_join_nr 0 0 0
	fi

	# no subflow IPv6 to v4 address even if v6 has a valid v4 at the end
	if reset "no JOIN with diff families v4-v6-2"; then
		pm_nl_set_limits $ns1 0 1
		pm_nl_set_limits $ns2 0 1
		pm_nl_add_endpoint $ns2 dead:beef:2::10.0.3.2 flags subflow
		run_tests $ns1 $ns2 10.0.1.1
		chk_join_nr 0 0 0
	fi

	# no subflow IPv4 to v6 address, no need to slow down too then
	if reset "no JOIN with diff families v6-v4"; then
		pm_nl_set_limits $ns1 0 1
		pm_nl_set_limits $ns2 0 1
		pm_nl_add_endpoint $ns2 10.0.3.2 flags subflow
		run_tests $ns1 $ns2 dead:beef:1::1
		chk_join_nr 0 0 0
	fi
}

backup_tests()
{
	# single subflow, backup
	if reset "single subflow, backup"; then
		pm_nl_set_limits $ns1 0 1
		pm_nl_set_limits $ns2 0 1
		pm_nl_add_endpoint $ns2 10.0.3.2 flags subflow,backup
		run_tests $ns1 $ns2 10.0.1.1 0 0 0 slow nobackup
		chk_join_nr 1 1 1
		chk_prio_nr 0 1
	fi

	# single address, backup
	if reset "single address, backup"; then
		pm_nl_set_limits $ns1 0 1
		pm_nl_add_endpoint $ns1 10.0.2.1 flags signal
		pm_nl_set_limits $ns2 1 1
		run_tests $ns1 $ns2 10.0.1.1 0 0 0 slow backup
		chk_join_nr 1 1 1
		chk_add_nr 1 1
		chk_prio_nr 1 1
	fi

	# single address with port, backup
	if reset "single address with port, backup"; then
		pm_nl_set_limits $ns1 0 1
		pm_nl_add_endpoint $ns1 10.0.2.1 flags signal port 10100
		pm_nl_set_limits $ns2 1 1
		run_tests $ns1 $ns2 10.0.1.1 0 0 0 slow backup
		chk_join_nr 1 1 1
		chk_add_nr 1 1
		chk_prio_nr 1 1
	fi

	if reset "mpc backup"; then
		pm_nl_add_endpoint $ns2 10.0.1.2 flags subflow,backup
		run_tests $ns1 $ns2 10.0.1.1 0 0 0 slow
		chk_join_nr 0 0 0
		chk_prio_nr 0 1
	fi

	if reset "mpc backup both sides"; then
		pm_nl_add_endpoint $ns1 10.0.1.1 flags subflow,backup
		pm_nl_add_endpoint $ns2 10.0.1.2 flags subflow,backup
		run_tests $ns1 $ns2 10.0.1.1 0 0 0 slow
		chk_join_nr 0 0 0
		chk_prio_nr 1 1
	fi

	if reset "mpc switch to backup"; then
		pm_nl_add_endpoint $ns2 10.0.1.2 flags subflow
		run_tests $ns1 $ns2 10.0.1.1 0 0 0 slow backup
		chk_join_nr 0 0 0
		chk_prio_nr 0 1
	fi

	if reset "mpc switch to backup both sides"; then
		pm_nl_add_endpoint $ns1 10.0.1.1 flags subflow
		pm_nl_add_endpoint $ns2 10.0.1.2 flags subflow
		run_tests $ns1 $ns2 10.0.1.1 0 0 0 slow backup
		chk_join_nr 0 0 0
		chk_prio_nr 1 1
	fi
}

LISTENER_CREATED=15 #MPTCP_EVENT_LISTENER_CREATED
LISTENER_CLOSED=16  #MPTCP_EVENT_LISTENER_CLOSED

AF_INET=2
AF_INET6=10

verify_listener_events()
{
	local evt=$1
	local e_type=$2
	local e_family=$3
	local e_saddr=$4
	local e_sport=$5
	local type
	local family
	local saddr
	local sport

	if [ $e_type = $LISTENER_CREATED ]; then
		stdbuf -o0 -e0 printf "\t\t\t\t\t CREATE_LISTENER %s:%s"\
			$e_saddr $e_sport
	elif [ $e_type = $LISTENER_CLOSED ]; then
		stdbuf -o0 -e0 printf "\t\t\t\t\t CLOSE_LISTENER %s:%s "\
			$e_saddr $e_sport
	fi

	type=$(grep "type:$e_type," $evt |
	       sed --unbuffered -n 's/.*\(type:\)\([[:digit:]]*\).*$/\2/p;q')
	family=$(grep "type:$e_type," $evt |
		 sed --unbuffered -n 's/.*\(family:\)\([[:digit:]]*\).*$/\2/p;q')
	sport=$(grep "type:$e_type," $evt |
		sed --unbuffered -n 's/.*\(sport:\)\([[:digit:]]*\).*$/\2/p;q')
	if [ $family ] && [ $family = $AF_INET6 ]; then
		saddr=$(grep "type:$e_type," $evt |
			sed --unbuffered -n 's/.*\(saddr6:\)\([0-9a-f:.]*\).*$/\2/p;q')
	else
		saddr=$(grep "type:$e_type," $evt |
			sed --unbuffered -n 's/.*\(saddr4:\)\([0-9.]*\).*$/\2/p;q')
	fi

	if [ $type ] && [ $type = $e_type ] &&
	   [ $family ] && [ $family = $e_family ] &&
	   [ $saddr ] && [ $saddr = $e_saddr ] &&
	   [ $sport ] && [ $sport = $e_sport ]; then
		stdbuf -o0 -e0 printf "[ ok ]\n"
		return 0
	fi
	fail_test
	stdbuf -o0 -e0 printf "[fail]\n"
}

add_addr_ports_tests()
{
	# signal address with port
	if reset "signal address with port"; then
		pm_nl_set_limits $ns1 0 1
		pm_nl_set_limits $ns2 1 1
		pm_nl_add_endpoint $ns1 10.0.2.1 flags signal port 10100
		run_tests $ns1 $ns2 10.0.1.1
		chk_join_nr 1 1 1
		chk_add_nr 1 1 1
	fi

	# subflow and signal with port
	if reset "subflow and signal with port"; then
		pm_nl_add_endpoint $ns1 10.0.2.1 flags signal port 10100
		pm_nl_set_limits $ns1 0 2
		pm_nl_set_limits $ns2 1 2
		pm_nl_add_endpoint $ns2 10.0.3.2 flags subflow
		run_tests $ns1 $ns2 10.0.1.1
		chk_join_nr 2 2 2
		chk_add_nr 1 1 1
	fi

	# single address with port, remove
	# pm listener events
	if reset_with_events "remove single address with port"; then
		pm_nl_set_limits $ns1 0 1
		pm_nl_add_endpoint $ns1 10.0.2.1 flags signal port 10100
		pm_nl_set_limits $ns2 1 1
		run_tests $ns1 $ns2 10.0.1.1 0 -1 0 slow
		chk_join_nr 1 1 1
		chk_add_nr 1 1 1
		chk_rm_nr 1 1 invert

		verify_listener_events $evts_ns1 $LISTENER_CREATED $AF_INET 10.0.2.1 10100
		verify_listener_events $evts_ns1 $LISTENER_CLOSED $AF_INET 10.0.2.1 10100
		kill_events_pids
	fi

	# subflow and signal with port, remove
	if reset "remove subflow and signal with port"; then
		pm_nl_set_limits $ns1 0 2
		pm_nl_add_endpoint $ns1 10.0.2.1 flags signal port 10100
		pm_nl_set_limits $ns2 1 2
		pm_nl_add_endpoint $ns2 10.0.3.2 flags subflow
		run_tests $ns1 $ns2 10.0.1.1 0 -1 -1 slow
		chk_join_nr 2 2 2
		chk_add_nr 1 1 1
		chk_rm_nr 1 1
	fi

	# subflows and signal with port, flush
	if reset "flush subflows and signal with port"; then
		pm_nl_set_limits $ns1 0 3
		pm_nl_add_endpoint $ns1 10.0.2.1 flags signal port 10100
		pm_nl_set_limits $ns2 1 3
		pm_nl_add_endpoint $ns2 10.0.3.2 flags subflow
		pm_nl_add_endpoint $ns2 10.0.4.2 flags subflow
		run_tests $ns1 $ns2 10.0.1.1 0 -8 -2 slow
		chk_join_nr 3 3 3
		chk_add_nr 1 1
		chk_rm_nr 1 3 invert simult
	fi

	# multiple addresses with port
	if reset "multiple addresses with port"; then
		pm_nl_set_limits $ns1 2 2
		pm_nl_add_endpoint $ns1 10.0.2.1 flags signal port 10100
		pm_nl_add_endpoint $ns1 10.0.3.1 flags signal port 10100
		pm_nl_set_limits $ns2 2 2
		run_tests $ns1 $ns2 10.0.1.1
		chk_join_nr 2 2 2
		chk_add_nr 2 2 2
	fi

	# multiple addresses with ports
	if reset "multiple addresses with ports"; then
		pm_nl_set_limits $ns1 2 2
		pm_nl_add_endpoint $ns1 10.0.2.1 flags signal port 10100
		pm_nl_add_endpoint $ns1 10.0.3.1 flags signal port 10101
		pm_nl_set_limits $ns2 2 2
		run_tests $ns1 $ns2 10.0.1.1
		chk_join_nr 2 2 2
		chk_add_nr 2 2 2
	fi
}

syncookies_tests()
{
	# single subflow, syncookies
	if reset_with_cookies "single subflow with syn cookies"; then
		pm_nl_set_limits $ns1 0 1
		pm_nl_set_limits $ns2 0 1
		pm_nl_add_endpoint $ns2 10.0.3.2 flags subflow
		run_tests $ns1 $ns2 10.0.1.1
		chk_join_nr 1 1 1
	fi

	# multiple subflows with syn cookies
	if reset_with_cookies "multiple subflows with syn cookies"; then
		pm_nl_set_limits $ns1 0 2
		pm_nl_set_limits $ns2 0 2
		pm_nl_add_endpoint $ns2 10.0.3.2 flags subflow
		pm_nl_add_endpoint $ns2 10.0.2.2 flags subflow
		run_tests $ns1 $ns2 10.0.1.1
		chk_join_nr 2 2 2
	fi

	# multiple subflows limited by server
	if reset_with_cookies "subflows limited by server w cookies"; then
		pm_nl_set_limits $ns1 0 1
		pm_nl_set_limits $ns2 0 2
		pm_nl_add_endpoint $ns2 10.0.3.2 flags subflow
		pm_nl_add_endpoint $ns2 10.0.2.2 flags subflow
		run_tests $ns1 $ns2 10.0.1.1
		chk_join_nr 2 1 1
	fi

	# test signal address with cookies
	if reset_with_cookies "signal address with syn cookies"; then
		pm_nl_set_limits $ns1 0 1
		pm_nl_set_limits $ns2 1 1
		pm_nl_add_endpoint $ns1 10.0.2.1 flags signal
		run_tests $ns1 $ns2 10.0.1.1
		chk_join_nr 1 1 1
		chk_add_nr 1 1
	fi

	# test cookie with subflow and signal
	if reset_with_cookies "subflow and signal w cookies"; then
		pm_nl_add_endpoint $ns1 10.0.2.1 flags signal
		pm_nl_set_limits $ns1 0 2
		pm_nl_set_limits $ns2 1 2
		pm_nl_add_endpoint $ns2 10.0.3.2 flags subflow
		run_tests $ns1 $ns2 10.0.1.1
		chk_join_nr 2 2 2
		chk_add_nr 1 1
	fi

	# accept and use add_addr with additional subflows
	if reset_with_cookies "subflows and signal w. cookies"; then
		pm_nl_set_limits $ns1 0 3
		pm_nl_add_endpoint $ns1 10.0.2.1 flags signal
		pm_nl_set_limits $ns2 1 3
		pm_nl_add_endpoint $ns2 10.0.3.2 flags subflow
		pm_nl_add_endpoint $ns2 10.0.4.2 flags subflow
		run_tests $ns1 $ns2 10.0.1.1
		chk_join_nr 3 3 3
		chk_add_nr 1 1
	fi
}

checksum_tests()
{
	# checksum test 0 0
	if reset_with_checksum 0 0; then
		pm_nl_set_limits $ns1 0 1
		pm_nl_set_limits $ns2 0 1
		run_tests $ns1 $ns2 10.0.1.1
		chk_join_nr 0 0 0
	fi

	# checksum test 1 1
	if reset_with_checksum 1 1; then
		pm_nl_set_limits $ns1 0 1
		pm_nl_set_limits $ns2 0 1
		run_tests $ns1 $ns2 10.0.1.1
		chk_join_nr 0 0 0
	fi

	# checksum test 0 1
	if reset_with_checksum 0 1; then
		pm_nl_set_limits $ns1 0 1
		pm_nl_set_limits $ns2 0 1
		run_tests $ns1 $ns2 10.0.1.1
		chk_join_nr 0 0 0
	fi

	# checksum test 1 0
	if reset_with_checksum 1 0; then
		pm_nl_set_limits $ns1 0 1
		pm_nl_set_limits $ns2 0 1
		run_tests $ns1 $ns2 10.0.1.1
		chk_join_nr 0 0 0
	fi
}

deny_join_id0_tests()
{
	# subflow allow join id0 ns1
	if reset_with_allow_join_id0 "single subflow allow join id0 ns1" 1 0; then
		pm_nl_set_limits $ns1 1 1
		pm_nl_set_limits $ns2 1 1
		pm_nl_add_endpoint $ns2 10.0.3.2 flags subflow
		run_tests $ns1 $ns2 10.0.1.1
		chk_join_nr 1 1 1
	fi

	# subflow allow join id0 ns2
	if reset_with_allow_join_id0 "single subflow allow join id0 ns2" 0 1; then
		pm_nl_set_limits $ns1 1 1
		pm_nl_set_limits $ns2 1 1
		pm_nl_add_endpoint $ns2 10.0.3.2 flags subflow
		run_tests $ns1 $ns2 10.0.1.1
		chk_join_nr 0 0 0
	fi

	# signal address allow join id0 ns1
	# ADD_ADDRs are not affected by allow_join_id0 value.
	if reset_with_allow_join_id0 "signal address allow join id0 ns1" 1 0; then
		pm_nl_set_limits $ns1 1 1
		pm_nl_set_limits $ns2 1 1
		pm_nl_add_endpoint $ns1 10.0.2.1 flags signal
		run_tests $ns1 $ns2 10.0.1.1
		chk_join_nr 1 1 1
		chk_add_nr 1 1
	fi

	# signal address allow join id0 ns2
	# ADD_ADDRs are not affected by allow_join_id0 value.
	if reset_with_allow_join_id0 "signal address allow join id0 ns2" 0 1; then
		pm_nl_set_limits $ns1 1 1
		pm_nl_set_limits $ns2 1 1
		pm_nl_add_endpoint $ns1 10.0.2.1 flags signal
		run_tests $ns1 $ns2 10.0.1.1
		chk_join_nr 1 1 1
		chk_add_nr 1 1
	fi

	# subflow and address allow join id0 ns1
	if reset_with_allow_join_id0 "subflow and address allow join id0 1" 1 0; then
		pm_nl_set_limits $ns1 2 2
		pm_nl_set_limits $ns2 2 2
		pm_nl_add_endpoint $ns1 10.0.2.1 flags signal
		pm_nl_add_endpoint $ns2 10.0.3.2 flags subflow
		run_tests $ns1 $ns2 10.0.1.1
		chk_join_nr 2 2 2
	fi

	# subflow and address allow join id0 ns2
	if reset_with_allow_join_id0 "subflow and address allow join id0 2" 0 1; then
		pm_nl_set_limits $ns1 2 2
		pm_nl_set_limits $ns2 2 2
		pm_nl_add_endpoint $ns1 10.0.2.1 flags signal
		pm_nl_add_endpoint $ns2 10.0.3.2 flags subflow
		run_tests $ns1 $ns2 10.0.1.1
		chk_join_nr 1 1 1
	fi
}

fullmesh_tests()
{
	# fullmesh 1
	# 2 fullmesh addrs in ns2, added before the connection,
	# 1 non-fullmesh addr in ns1, added during the connection.
	if reset "fullmesh test 2x1"; then
		pm_nl_set_limits $ns1 0 4
		pm_nl_set_limits $ns2 1 4
		pm_nl_add_endpoint $ns2 10.0.2.2 flags subflow,fullmesh
		pm_nl_add_endpoint $ns2 10.0.3.2 flags subflow,fullmesh
		run_tests $ns1 $ns2 10.0.1.1 0 1 0 slow
		chk_join_nr 4 4 4
		chk_add_nr 1 1
	fi

	# fullmesh 2
	# 1 non-fullmesh addr in ns1, added before the connection,
	# 1 fullmesh addr in ns2, added during the connection.
	if reset "fullmesh test 1x1"; then
		pm_nl_set_limits $ns1 1 3
		pm_nl_set_limits $ns2 1 3
		pm_nl_add_endpoint $ns1 10.0.2.1 flags signal
		run_tests $ns1 $ns2 10.0.1.1 0 0 fullmesh_1 slow
		chk_join_nr 3 3 3
		chk_add_nr 1 1
	fi

	# fullmesh 3
	# 1 non-fullmesh addr in ns1, added before the connection,
	# 2 fullmesh addrs in ns2, added during the connection.
	if reset "fullmesh test 1x2"; then
		pm_nl_set_limits $ns1 2 5
		pm_nl_set_limits $ns2 1 5
		pm_nl_add_endpoint $ns1 10.0.2.1 flags signal
		run_tests $ns1 $ns2 10.0.1.1 0 0 fullmesh_2 slow
		chk_join_nr 5 5 5
		chk_add_nr 1 1
	fi

	# fullmesh 4
	# 1 non-fullmesh addr in ns1, added before the connection,
	# 2 fullmesh addrs in ns2, added during the connection,
	# limit max_subflows to 4.
	if reset "fullmesh test 1x2, limited"; then
		pm_nl_set_limits $ns1 2 4
		pm_nl_set_limits $ns2 1 4
		pm_nl_add_endpoint $ns1 10.0.2.1 flags signal
		run_tests $ns1 $ns2 10.0.1.1 0 0 fullmesh_2 slow
		chk_join_nr 4 4 4
		chk_add_nr 1 1
	fi

	# set fullmesh flag
	if reset "set fullmesh flag test"; then
		pm_nl_set_limits $ns1 4 4
		pm_nl_add_endpoint $ns1 10.0.2.1 flags subflow
		pm_nl_set_limits $ns2 4 4
		run_tests $ns1 $ns2 10.0.1.1 0 0 1 slow fullmesh
		chk_join_nr 2 2 2
		chk_rm_nr 0 1
	fi

	# set nofullmesh flag
	if reset "set nofullmesh flag test"; then
		pm_nl_set_limits $ns1 4 4
		pm_nl_add_endpoint $ns1 10.0.2.1 flags subflow,fullmesh
		pm_nl_set_limits $ns2 4 4
		run_tests $ns1 $ns2 10.0.1.1 0 0 fullmesh_1 slow nofullmesh
		chk_join_nr 2 2 2
		chk_rm_nr 0 1
	fi

	# set backup,fullmesh flags
	if reset "set backup,fullmesh flags test"; then
		pm_nl_set_limits $ns1 4 4
		pm_nl_add_endpoint $ns1 10.0.2.1 flags subflow
		pm_nl_set_limits $ns2 4 4
		run_tests $ns1 $ns2 10.0.1.1 0 0 1 slow backup,fullmesh
		chk_join_nr 2 2 2
		chk_prio_nr 0 1
		chk_rm_nr 0 1
	fi

	# set nobackup,nofullmesh flags
	if reset "set nobackup,nofullmesh flags test"; then
		pm_nl_set_limits $ns1 4 4
		pm_nl_set_limits $ns2 4 4
		pm_nl_add_endpoint $ns2 10.0.2.2 flags subflow,backup,fullmesh
		run_tests $ns1 $ns2 10.0.1.1 0 0 0 slow nobackup,nofullmesh
		chk_join_nr 2 2 2
		chk_prio_nr 0 1
		chk_rm_nr 0 1
	fi
}

fastclose_tests()
{
	if reset "fastclose test"; then
		run_tests $ns1 $ns2 10.0.1.1 1024 0 fastclose_client
		chk_join_nr 0 0 0
		chk_fclose_nr 1 1
		chk_rst_nr 1 1 invert
	fi

	if reset "fastclose server test"; then
		run_tests $ns1 $ns2 10.0.1.1 1024 0 fastclose_server
		chk_join_nr 0 0 0
		chk_fclose_nr 1 1 invert
		chk_rst_nr 1 1
	fi
}

pedit_action_pkts()
{
	tc -n $ns2 -j -s action show action pedit index 100 | \
		grep "packets" | \
		sed 's/.*"packets":\([0-9]\+\),.*/\1/'
}

fail_tests()
{
	# single subflow
	if reset_with_fail "Infinite map" 1; then
		run_tests $ns1 $ns2 10.0.1.1 128
		chk_join_nr 0 0 0 +1 +0 1 0 1 "$(pedit_action_pkts)"
		chk_fail_nr 1 -1 invert
	fi

	# multiple subflows
	if reset_with_fail "MP_FAIL MP_RST" 2; then
		tc -n $ns2 qdisc add dev ns2eth1 root netem rate 1mbit delay 5
		pm_nl_set_limits $ns1 0 1
		pm_nl_set_limits $ns2 0 1
		pm_nl_add_endpoint $ns2 10.0.2.2 dev ns2eth2 flags subflow
		run_tests $ns1 $ns2 10.0.1.1 1024
		chk_join_nr 1 1 1 1 0 1 1 0 "$(pedit_action_pkts)"
	fi
}

userspace_tests()
{
	# userspace pm type prevents add_addr
	if reset "userspace pm type prevents add_addr"; then
		set_userspace_pm $ns1
		pm_nl_set_limits $ns1 0 2
		pm_nl_set_limits $ns2 0 2
		pm_nl_add_endpoint $ns1 10.0.2.1 flags signal
		run_tests $ns1 $ns2 10.0.1.1
		chk_join_nr 0 0 0
		chk_add_nr 0 0
	fi

	# userspace pm type does not echo add_addr without daemon
	if reset "userspace pm no echo w/o daemon"; then
		set_userspace_pm $ns2
		pm_nl_set_limits $ns1 0 2
		pm_nl_set_limits $ns2 0 2
		pm_nl_add_endpoint $ns1 10.0.2.1 flags signal
		run_tests $ns1 $ns2 10.0.1.1
		chk_join_nr 0 0 0
		chk_add_nr 1 0
	fi

	# userspace pm type rejects join
	if reset "userspace pm type rejects join"; then
		set_userspace_pm $ns1
		pm_nl_set_limits $ns1 1 1
		pm_nl_set_limits $ns2 1 1
		pm_nl_add_endpoint $ns2 10.0.3.2 flags subflow
		run_tests $ns1 $ns2 10.0.1.1
		chk_join_nr 1 1 0
	fi

	# userspace pm type does not send join
	if reset "userspace pm type does not send join"; then
		set_userspace_pm $ns2
		pm_nl_set_limits $ns1 1 1
		pm_nl_set_limits $ns2 1 1
		pm_nl_add_endpoint $ns2 10.0.3.2 flags subflow
		run_tests $ns1 $ns2 10.0.1.1
		chk_join_nr 0 0 0
	fi

	# userspace pm type prevents mp_prio
	if reset "userspace pm type prevents mp_prio"; then
		set_userspace_pm $ns1
		pm_nl_set_limits $ns1 1 1
		pm_nl_set_limits $ns2 1 1
		pm_nl_add_endpoint $ns2 10.0.3.2 flags subflow
		run_tests $ns1 $ns2 10.0.1.1 0 0 0 slow backup
		chk_join_nr 1 1 0
		chk_prio_nr 0 0
	fi

	# userspace pm type prevents rm_addr
	if reset "userspace pm type prevents rm_addr"; then
		set_userspace_pm $ns1
		set_userspace_pm $ns2
		pm_nl_set_limits $ns1 0 1
		pm_nl_set_limits $ns2 0 1
		pm_nl_add_endpoint $ns2 10.0.3.2 flags subflow
		run_tests $ns1 $ns2 10.0.1.1 0 0 -1 slow
		chk_join_nr 0 0 0
		chk_rm_nr 0 0
	fi

	# userspace pm add & remove address
	if reset_with_events "userspace pm add & remove address"; then
		set_userspace_pm $ns1
		pm_nl_set_limits $ns2 1 1
		run_tests $ns1 $ns2 10.0.1.1 0 userspace_1 0 slow
		chk_join_nr 1 1 1
		chk_add_nr 1 1
		chk_rm_nr 1 1 invert
		kill_events_pids
	fi

	# userspace pm create destroy subflow
	if reset_with_events "userspace pm create destroy subflow"; then
		set_userspace_pm $ns2
		pm_nl_set_limits $ns1 0 1
		run_tests $ns1 $ns2 10.0.1.1 0 0 userspace_1 slow
		chk_join_nr 1 1 1
		chk_rm_nr 0 1
		kill_events_pids
	fi
}

endpoint_tests()
{
	# userspace pm type prevents add_addr
	if reset "implicit EP"; then
		pm_nl_set_limits $ns1 2 2
		pm_nl_set_limits $ns2 2 2
		pm_nl_add_endpoint $ns1 10.0.2.1 flags signal
		run_tests $ns1 $ns2 10.0.1.1 0 0 0 slow 2>/dev/null &

		wait_mpj $ns1
		pm_nl_check_endpoint 1 "creation" \
			$ns2 10.0.2.2 id 1 flags implicit

		pm_nl_add_endpoint $ns2 10.0.2.2 id 33
		pm_nl_check_endpoint 0 "ID change is prevented" \
			$ns2 10.0.2.2 id 1 flags implicit

		pm_nl_add_endpoint $ns2 10.0.2.2 flags signal
		pm_nl_check_endpoint 0 "modif is allowed" \
			$ns2 10.0.2.2 id 1 flags signal
		kill_tests_wait
	fi

	if reset "delete and re-add"; then
		pm_nl_set_limits $ns1 1 1
		pm_nl_set_limits $ns2 1 1
		pm_nl_add_endpoint $ns2 10.0.2.2 id 2 dev ns2eth2 flags subflow
		run_tests $ns1 $ns2 10.0.1.1 4 0 0 speed_20 2>/dev/null &

		wait_mpj $ns2
		pm_nl_del_endpoint $ns2 2 10.0.2.2
		sleep 0.5
		chk_subflow_nr needtitle "after delete" 1

		pm_nl_add_endpoint $ns2 10.0.2.2 dev ns2eth2 flags subflow
		wait_mpj $ns2
		chk_subflow_nr "" "after re-add" 2
		kill_tests_wait
	fi
}

# [$1: error message]
usage()
{
	if [ -n "${1}" ]; then
		echo "${1}"
		ret=1
	fi

	echo "mptcp_join usage:"

	local key
	for key in "${!all_tests[@]}"; do
		echo "  -${key} ${all_tests[${key}]}"
	done

	echo "  -c capture pcap files"
	echo "  -C enable data checksum"
	echo "  -i use ip mptcp"
	echo "  -h help"

	echo "[test ids|names]"

	exit ${ret}
}


# Use a "simple" array to force an specific order we cannot have with an associative one
all_tests_sorted=(
	f@subflows_tests
	e@subflows_error_tests
	s@signal_address_tests
	l@link_failure_tests
	t@add_addr_timeout_tests
	r@remove_tests
	a@add_tests
	6@ipv6_tests
	4@v4mapped_tests
	b@backup_tests
	p@add_addr_ports_tests
	k@syncookies_tests
	S@checksum_tests
	d@deny_join_id0_tests
	m@fullmesh_tests
	z@fastclose_tests
	F@fail_tests
	u@userspace_tests
	I@endpoint_tests
)

all_tests_args=""
all_tests_names=()
for subtests in "${all_tests_sorted[@]}"; do
	key="${subtests%@*}"
	value="${subtests#*@}"

	all_tests_args+="${key}"
	all_tests_names+=("${value}")
	all_tests[${key}]="${value}"
done

tests=()
while getopts "${all_tests_args}cCih" opt; do
	case $opt in
		["${all_tests_args}"])
			tests+=("${all_tests[${opt}]}")
			;;
		c)
			capture=1
			;;
		C)
			checksum=1
			;;
		i)
			ip_mptcp=1
			;;
		h)
			usage
			;;
		*)
			usage "Unknown option: -${opt}"
			;;
	esac
done

shift $((OPTIND - 1))

for arg in "${@}"; do
	if [[ "${arg}" =~ ^[0-9]+$ ]]; then
		only_tests_ids+=("${arg}")
	else
		only_tests_names+=("${arg}")
	fi
done

if [ ${#tests[@]} -eq 0 ]; then
	tests=("${all_tests_names[@]}")
fi

for subtests in "${tests[@]}"; do
	"${subtests}"
done

if [ ${ret} -ne 0 ]; then
	echo
	echo "${#failed_tests[@]} failure(s) has(ve) been detected:"
	for i in $(get_failed_tests_ids); do
		echo -e "\t- ${i}: ${failed_tests[${i}]}"
	done
	echo
fi

exit $ret
