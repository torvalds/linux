#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

. "$(dirname "${0}")/mptcp_lib.sh"

ret=0
sin=""
sout=""
cin=""
cout=""
ksft_skip=4
timeout_poll=30
timeout_test=$((timeout_poll * 2 + 1))
mptcp_connect=""
iptables="iptables"
ip6tables="ip6tables"

sec=$(date +%s)
rndh=$(printf %x $sec)-$(mktemp -u XXXXXX)
ns1="ns1-$rndh"
ns2="ns2-$rndh"
ns_sbox="ns_sbox-$rndh"

add_mark_rules()
{
	local ns=$1
	local m=$2

	local t
	for t in ${iptables} ${ip6tables}; do
		# just to debug: check we have multiple subflows connection requests
		ip netns exec $ns $t -A OUTPUT -p tcp --syn -m mark --mark $m -j ACCEPT

		# RST packets might be handled by a internal dummy socket
		ip netns exec $ns $t -A OUTPUT -p tcp --tcp-flags RST RST -m mark --mark 0 -j ACCEPT

		ip netns exec $ns $t -A OUTPUT -p tcp -m mark --mark $m -j ACCEPT
		ip netns exec $ns $t -A OUTPUT -p tcp -m mark --mark 0 -j DROP
	done
}

init()
{
	local netns
	for netns in "$ns1" "$ns2" "$ns_sbox";do
		ip netns add $netns || exit $ksft_skip
		ip -net $netns link set lo up
		ip netns exec $netns sysctl -q net.mptcp.enabled=1
		ip netns exec $netns sysctl -q net.ipv4.conf.all.rp_filter=0
		ip netns exec $netns sysctl -q net.ipv4.conf.default.rp_filter=0
	done

	local i
	for i in `seq 1 4`; do
		ip link add ns1eth$i netns "$ns1" type veth peer name ns2eth$i netns "$ns2"
		ip -net "$ns1" addr add 10.0.$i.1/24 dev ns1eth$i
		ip -net "$ns1" addr add dead:beef:$i::1/64 dev ns1eth$i nodad
		ip -net "$ns1" link set ns1eth$i up

		ip -net "$ns2" addr add 10.0.$i.2/24 dev ns2eth$i
		ip -net "$ns2" addr add dead:beef:$i::2/64 dev ns2eth$i nodad
		ip -net "$ns2" link set ns2eth$i up

		# let $ns2 reach any $ns1 address from any interface
		ip -net "$ns2" route add default via 10.0.$i.1 dev ns2eth$i metric 10$i

		ip netns exec $ns1 ./pm_nl_ctl add 10.0.$i.1 flags signal
		ip netns exec $ns1 ./pm_nl_ctl add dead:beef:$i::1 flags signal

		ip netns exec $ns2 ./pm_nl_ctl add 10.0.$i.2 flags signal
		ip netns exec $ns2 ./pm_nl_ctl add dead:beef:$i::2 flags signal
	done

	ip netns exec $ns1 ./pm_nl_ctl limits 8 8
	ip netns exec $ns2 ./pm_nl_ctl limits 8 8

	add_mark_rules $ns1 1
	add_mark_rules $ns2 2
}

cleanup()
{
	local netns
	for netns in "$ns1" "$ns2" "$ns_sbox"; do
		ip netns del $netns
	done
	rm -f "$cin" "$cout"
	rm -f "$sin" "$sout"
}

mptcp_lib_check_mptcp
mptcp_lib_check_kallsyms

ip -Version > /dev/null 2>&1
if [ $? -ne 0 ];then
	echo "SKIP: Could not run test without ip tool"
	exit $ksft_skip
fi

# Use the legacy version if available to support old kernel versions
if iptables-legacy -V &> /dev/null; then
	iptables="iptables-legacy"
	ip6tables="ip6tables-legacy"
elif ! iptables -V &> /dev/null; then
	echo "SKIP: Could not run all tests without iptables tool"
	exit $ksft_skip
elif ! ip6tables -V &> /dev/null; then
	echo "SKIP: Could not run all tests without ip6tables tool"
	exit $ksft_skip
fi

check_mark()
{
	local ns=$1
	local af=$2

	local tables=${iptables}

	if [ $af -eq 6 ];then
		tables=${ip6tables}
	fi

	local counters values
	counters=$(ip netns exec $ns $tables -v -L OUTPUT | grep DROP)
	values=${counters%DROP*}

	local v
	for v in $values; do
		if [ $v -ne 0 ]; then
			echo "FAIL: got $tables $values in ns $ns , not 0 - not all expected packets marked" 1>&2
			ret=1
			return 1
		fi
	done

	return 0
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

	cmp "$in" "$out" > /dev/null 2>&1
	if [ $? -ne 0 ] ;then
		echo "[ FAIL ] $what does not match (in, out):"
		print_file_err "$in"
		print_file_err "$out"
		ret=1

		return 1
	fi

	return 0
}

do_transfer()
{
	local listener_ns="$1"
	local connector_ns="$2"
	local cl_proto="$3"
	local srv_proto="$4"
	local connect_addr="$5"

	local port=12001

	:> "$cout"
	:> "$sout"

	local mptcp_connect="./mptcp_connect -r 20"

	local local_addr ip
	if mptcp_lib_is_v6 "${connect_addr}"; then
		local_addr="::"
		ip=ipv6
	else
		local_addr="0.0.0.0"
		ip=ipv4
	fi

	cmsg="TIMESTAMPNS"
	if mptcp_lib_kallsyms_has "mptcp_ioctl$"; then
		cmsg+=",TCPINQ"
	fi

	timeout ${timeout_test} \
		ip netns exec ${listener_ns} \
			$mptcp_connect -t ${timeout_poll} -l -M 1 -p $port -s ${srv_proto} -c "${cmsg}" \
				${local_addr} < "$sin" > "$sout" &
	local spid=$!

	sleep 1

	timeout ${timeout_test} \
		ip netns exec ${connector_ns} \
			$mptcp_connect -t ${timeout_poll} -M 2 -p $port -s ${cl_proto} -c "${cmsg}" \
				$connect_addr < "$cin" > "$cout" &

	local cpid=$!

	wait $cpid
	local retc=$?
	wait $spid
	local rets=$?

	if [ ${rets} -ne 0 ] || [ ${retc} -ne 0 ]; then
		echo " client exit code $retc, server $rets" 1>&2
		echo -e "\nnetns ${listener_ns} socket stat for ${port}:" 1>&2
		ip netns exec ${listener_ns} ss -Menita 1>&2 -o "sport = :$port"

		echo -e "\nnetns ${connector_ns} socket stat for ${port}:" 1>&2
		ip netns exec ${connector_ns} ss -Menita 1>&2 -o "dport = :$port"

		mptcp_lib_result_fail "transfer ${ip}"

		ret=1
		return 1
	fi

	if [ $local_addr = "::" ];then
		check_mark $listener_ns 6 || retc=1
		check_mark $connector_ns 6 || retc=1
	else
		check_mark $listener_ns 4 || retc=1
		check_mark $connector_ns 4 || retc=1
	fi

	check_transfer $cin $sout "file received by server"
	rets=$?

	mptcp_lib_result_code "${retc}" "mark ${ip}"
	mptcp_lib_result_code "${rets}" "transfer ${ip}"

	if [ $retc -eq 0 ] && [ $rets -eq 0 ];then
		return 0
	fi

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

do_mptcp_sockopt_tests()
{
	local lret=0

	if ! mptcp_lib_kallsyms_has "mptcp_diag_fill_info$"; then
		echo "INFO: MPTCP sockopt not supported: SKIP"
		mptcp_lib_result_skip "sockopt"
		return
	fi

	ip netns exec "$ns_sbox" ./mptcp_sockopt
	lret=$?

	if [ $lret -ne 0 ]; then
		echo "FAIL: SOL_MPTCP getsockopt" 1>&2
		mptcp_lib_result_fail "sockopt v4"
		ret=$lret
		return
	fi
	mptcp_lib_result_pass "sockopt v4"

	ip netns exec "$ns_sbox" ./mptcp_sockopt -6
	lret=$?

	if [ $lret -ne 0 ]; then
		echo "FAIL: SOL_MPTCP getsockopt (ipv6)" 1>&2
		mptcp_lib_result_fail "sockopt v6"
		ret=$lret
		return
	fi
	mptcp_lib_result_pass "sockopt v6"
}

run_tests()
{
	local listener_ns="$1"
	local connector_ns="$2"
	local connect_addr="$3"
	local lret=0

	do_transfer ${listener_ns} ${connector_ns} MPTCP MPTCP ${connect_addr}

	lret=$?

	if [ $lret -ne 0 ]; then
		ret=$lret
		return
	fi
}

do_tcpinq_test()
{
	ip netns exec "$ns_sbox" ./mptcp_inq "$@"
	local lret=$?
	if [ $lret -ne 0 ];then
		ret=$lret
		echo "FAIL: mptcp_inq $@" 1>&2
		mptcp_lib_result_fail "TCP_INQ: $*"
		return $lret
	fi

	echo "PASS: TCP_INQ cmsg/ioctl $@"
	mptcp_lib_result_pass "TCP_INQ: $*"
	return $lret
}

do_tcpinq_tests()
{
	local lret=0

	if ! mptcp_lib_kallsyms_has "mptcp_ioctl$"; then
		echo "INFO: TCP_INQ not supported: SKIP"
		mptcp_lib_result_skip "TCP_INQ"
		return
	fi

	local args
	for args in "-t tcp" "-r tcp"; do
		do_tcpinq_test $args
		lret=$?
		if [ $lret -ne 0 ] ; then
			return $lret
		fi
		do_tcpinq_test -6 $args
		lret=$?
		if [ $lret -ne 0 ] ; then
			return $lret
		fi
	done

	do_tcpinq_test -r tcp -t tcp

	return $?
}

sin=$(mktemp)
sout=$(mktemp)
cin=$(mktemp)
cout=$(mktemp)
init
make_file "$cin" "client" 1
make_file "$sin" "server" 1
trap cleanup EXIT

run_tests $ns1 $ns2 10.0.1.1
run_tests $ns1 $ns2 dead:beef:1::1

if [ $ret -eq 0 ];then
	echo "PASS: all packets had packet mark set"
fi

do_mptcp_sockopt_tests
if [ $ret -eq 0 ];then
	echo "PASS: SOL_MPTCP getsockopt has expected information"
fi

do_tcpinq_tests

mptcp_lib_result_print_all_tap
exit $ret
