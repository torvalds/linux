#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Double quotes to prevent globbing and word splitting is recommended in new
# code but we accept it, especially because there were too many before having
# address all other issues detected by shellcheck.
#shellcheck disable=SC2086

. "$(dirname "${0}")/mptcp_lib.sh"

ret=0
sin=""
sout=""
cin=""
cout=""
timeout_poll=30
timeout_test=$((timeout_poll * 2 + 1))
iptables="iptables"
ip6tables="ip6tables"

ns1=""
ns2=""
ns_sbox=""

usage() {
	echo "Usage: $0 [ -i ] [ -h ]"
	echo -e "\t-i: use 'ip mptcp' instead of 'pm_nl_ctl'"
	echo -e "\t-h: help"
}

while getopts "hi" option;do
	case "$option" in
	"h")
		usage "$0"
		exit ${KSFT_PASS}
		;;
	"i")
		mptcp_lib_set_ip_mptcp
		;;
	"?")
		usage "$0"
		exit ${KSFT_FAIL}
		;;
	esac
done

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
	mptcp_lib_ns_init ns1 ns2 ns_sbox

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

		mptcp_lib_pm_nl_add_endpoint "${ns1}" "10.0.${i}.1" flags signal
		mptcp_lib_pm_nl_add_endpoint "${ns1}" "dead:beef:${i}::1" flags signal

		mptcp_lib_pm_nl_add_endpoint "${ns2}" "10.0.${i}.2" flags signal
		mptcp_lib_pm_nl_add_endpoint "${ns2}" "dead:beef:${i}::2" flags signal
	done

	mptcp_lib_pm_nl_set_limits "${ns1}" 8 8
	mptcp_lib_pm_nl_set_limits "${ns2}" 8 8

	add_mark_rules $ns1 1
	add_mark_rules $ns2 2
}

# This function is used in the cleanup trap
#shellcheck disable=SC2317,SC2329
cleanup()
{
	mptcp_lib_ns_exit "${ns1}" "${ns2}" "${ns_sbox}"
	rm -f "$cin" "$cout"
	rm -f "$sin" "$sout"
}

mptcp_lib_check_mptcp
mptcp_lib_check_kallsyms
mptcp_lib_check_tools ip "${iptables}" "${ip6tables}"

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
			mptcp_lib_pr_fail "got $tables $values in ns $ns," \
					  "not 0 - not all expected packets marked"
			ret=${KSFT_FAIL}
			return 1
		fi
	done

	return 0
}

print_title()
{
	mptcp_lib_print_title "${@}"
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

	NSTAT_HISTORY=/tmp/${listener_ns}.nstat ip netns exec ${listener_ns} \
		nstat -n
	NSTAT_HISTORY=/tmp/${connector_ns}.nstat ip netns exec ${connector_ns} \
		nstat -n

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

	NSTAT_HISTORY=/tmp/${listener_ns}.nstat ip netns exec ${listener_ns} \
		nstat | grep Tcp > /tmp/${listener_ns}.out
	NSTAT_HISTORY=/tmp/${connector_ns}.nstat ip netns exec ${connector_ns} \
		nstat | grep Tcp > /tmp/${connector_ns}.out

	print_title "Transfer ${ip:2}"
	if [ ${rets} -ne 0 ] || [ ${retc} -ne 0 ]; then
		mptcp_lib_pr_fail "client exit code $retc, server $rets"
		mptcp_lib_pr_err_stats "${listener_ns}" "${connector_ns}" "${port}" \
			"/tmp/${listener_ns}.out" "/tmp/${connector_ns}.out"

		mptcp_lib_result_fail "transfer ${ip}"

		ret=${KSFT_FAIL}
		return 1
	fi
	if ! mptcp_lib_check_transfer $cin $sout "file received by server"; then
		rets=1
	else
		mptcp_lib_pr_ok
	fi
	mptcp_lib_result_code "${rets}" "transfer ${ip}"

	print_title "Mark ${ip:2}"
	if [ $local_addr = "::" ];then
		check_mark $listener_ns 6 || retc=1
		check_mark $connector_ns 6 || retc=1
	else
		check_mark $listener_ns 4 || retc=1
		check_mark $connector_ns 4 || retc=1
	fi

	mptcp_lib_result_code "${retc}" "mark ${ip}"

	if [ $retc -eq 0 ] && [ $rets -eq 0 ];then
		mptcp_lib_pr_ok
		return 0
	fi
	mptcp_lib_pr_fail

	return 1
}

make_file()
{
	local name=$1
	local who=$2
	local size=$3

	mptcp_lib_make_file $name 1024 $size

	echo "Created $name (size $size KB) containing data sent by $who"
}

do_mptcp_sockopt_tests()
{
	local lret=0

	if ! mptcp_lib_kallsyms_has "mptcp_diag_fill_info$"; then
		mptcp_lib_pr_skip "MPTCP sockopt not supported"
		mptcp_lib_result_skip "sockopt"
		return
	fi

	ip netns exec "$ns_sbox" ./mptcp_sockopt
	lret=$?

	print_title "SOL_MPTCP sockopt v4"
	if [ $lret -ne 0 ]; then
		mptcp_lib_pr_fail
		mptcp_lib_result_fail "sockopt v4"
		ret=$lret
		return
	fi
	mptcp_lib_pr_ok
	mptcp_lib_result_pass "sockopt v4"

	ip netns exec "$ns_sbox" ./mptcp_sockopt -6
	lret=$?

	print_title "SOL_MPTCP sockopt v6"
	if [ $lret -ne 0 ]; then
		mptcp_lib_pr_fail
		mptcp_lib_result_fail "sockopt v6"
		ret=$lret
		return
	fi
	mptcp_lib_pr_ok
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
	print_title "TCP_INQ cmsg/ioctl $*"
	ip netns exec "$ns_sbox" ./mptcp_inq "$@"
	local lret=$?
	if [ $lret -ne 0 ];then
		ret=$lret
		mptcp_lib_pr_fail
		mptcp_lib_result_fail "TCP_INQ: $*"
		return $lret
	fi

	mptcp_lib_pr_ok
	mptcp_lib_result_pass "TCP_INQ: $*"
	return $lret
}

do_tcpinq_tests()
{
	local lret=0

	if ! mptcp_lib_kallsyms_has "mptcp_ioctl$"; then
		mptcp_lib_pr_skip "TCP_INQ not supported"
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
mptcp_lib_subtests_last_ts_reset

run_tests $ns1 $ns2 10.0.1.1
run_tests $ns1 $ns2 dead:beef:1::1

do_mptcp_sockopt_tests
do_tcpinq_tests

mptcp_lib_result_print_all_tap
exit $ret
