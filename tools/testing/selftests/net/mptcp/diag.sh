#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Double quotes to prevent globbing and word splitting is recommended in new
# code but we accept it, especially because there were too many before having
# address all other issues detected by shellcheck.
#shellcheck disable=SC2086

. "$(dirname "${0}")/mptcp_lib.sh"

ns=""
timeout_poll=30
timeout_test=$((timeout_poll * 2 + 1))
ret=0

flush_pids()
{
	# mptcp_connect in join mode will sleep a bit before completing,
	# give it some time
	sleep 1.1

	ip netns pids "${ns}" | xargs --no-run-if-empty kill -SIGUSR1 &>/dev/null

	for _ in $(seq $((timeout_poll * 10))); do
		[ -z "$(ip netns pids "${ns}")" ] && break
		sleep 0.1
	done
}

# This function is used in the cleanup trap
#shellcheck disable=SC2317
cleanup()
{
	ip netns pids "${ns}" | xargs --no-run-if-empty kill -SIGKILL &>/dev/null

	mptcp_lib_ns_exit "${ns}"
}

mptcp_lib_check_mptcp
mptcp_lib_check_tools ip ss

get_msk_inuse()
{
	ip netns exec $ns cat /proc/net/protocols | awk '$1~/^MPTCP$/{print $3}'
}

__chk_nr()
{
	local command="$1"
	local expected=$2
	local msg="$3"
	local skip="${4-SKIP}"
	local nr

	nr=$(eval $command)

	mptcp_lib_print_title "$msg"
	if [ "$nr" != "$expected" ]; then
		if [ "$nr" = "$skip" ] && ! mptcp_lib_expect_all_features; then
			mptcp_lib_pr_skip "Feature probably not supported"
			mptcp_lib_result_skip "${msg}"
		else
			mptcp_lib_pr_fail "expected $expected found $nr"
			mptcp_lib_result_fail "${msg}"
			ret=${KSFT_FAIL}
		fi
	else
		mptcp_lib_pr_ok
		mptcp_lib_result_pass "${msg}"
	fi
}

__chk_msk_nr()
{
	local condition=$1
	shift 1

	__chk_nr "ss -inmHMN $ns | $condition" "$@"
}

chk_msk_nr()
{
	__chk_msk_nr "grep -c token:" "$@"
}

chk_listener_nr()
{
	local expected=$1
	local msg="$2"

	__chk_nr "ss -nlHMON $ns | wc -l" "$expected" "$msg - mptcp" 0
	__chk_nr "ss -nlHtON $ns | wc -l" "$expected" "$msg - subflows"
}

wait_msk_nr()
{
	local condition="grep -c token:"
	local expected=$1
	local timeout=20
	local msg nr
	local max=0
	local i=0

	shift 1
	msg=$*

	while [ $i -lt $timeout ]; do
		nr=$(ss -inmHMN $ns | $condition)
		[ $nr == $expected ] && break;
		[ $nr -gt $max ] && max=$nr
		i=$((i + 1))
		sleep 1
	done

	mptcp_lib_print_title "$msg"
	if [ $i -ge $timeout ]; then
		mptcp_lib_pr_fail "timeout while expecting $expected max $max last $nr"
		mptcp_lib_result_fail "${msg} # timeout"
		ret=${KSFT_FAIL}
	elif [ $nr != $expected ]; then
		mptcp_lib_pr_fail "expected $expected found $nr"
		mptcp_lib_result_fail "${msg} # unexpected result"
		ret=${KSFT_FAIL}
	else
		mptcp_lib_pr_ok
		mptcp_lib_result_pass "${msg}"
	fi
}

chk_msk_fallback_nr()
{
	__chk_msk_nr "grep -c fallback" "$@"
}

chk_msk_remote_key_nr()
{
	__chk_msk_nr "grep -c remote_key" "$@"
}

__chk_listen()
{
	local filter="$1"
	local expected=$2
	local msg="$3"

	__chk_nr "ss -N $ns -Ml '$filter' | grep -c LISTEN" "$expected" "$msg" 0
}

chk_msk_listen()
{
	lport=$1

	# destination port search should always return empty list
	__chk_listen "dport $lport" 0 "listen match for dport $lport"

	# should return 'our' mptcp listen socket
	__chk_listen "sport $lport" 1 "listen match for sport $lport"

	__chk_listen "src inet:0.0.0.0:$lport" 1 "listen match for saddr and sport"

	__chk_listen "" 1 "all listen sockets"

	nr=$(ss -Ml $filter | wc -l)
}

chk_msk_inuse()
{
	local expected=$1
	local msg="....chk ${2:-${expected}} msk in use"
	local listen_nr

	if [ "${expected}" -eq 0 ]; then
		msg+=" after flush"
	fi

	listen_nr=$(ss -N "${ns}" -Ml | grep -c LISTEN)
	expected=$((expected + listen_nr))

	for _ in $(seq 10); do
		if [ "$(get_msk_inuse)" -eq $expected ]; then
			break
		fi
		sleep 0.1
	done

	__chk_nr get_msk_inuse $expected "${msg}" 0
}

# $1: cestab nr
chk_msk_cestab()
{
	local expected=$1
	local msg="....chk ${2:-${expected}} cestab"

	if [ "${expected}" -eq 0 ]; then
		msg+=" after flush"
	fi

	__chk_nr "mptcp_lib_get_counter ${ns} MPTcpExtMPCurrEstab" \
		 "${expected}" "${msg}" ""
}

chk_dump_one()
{
	local ss_token
	local token
	local msg

	ss_token="$(ss -inmHMN $ns | grep 'token:' |\
		    head -n 1 |\
		    sed 's/.*token:\([0-9a-f]*\).*/\1/')"

	token="$(ip netns exec $ns ./mptcp_diag -t $ss_token |\
		 awk -F':[ \t]+' '/^token/ {print $2}')"

	msg="....chk dump_one"

	mptcp_lib_print_title "$msg"
	if [ -n "$ss_token" ] && [ "$ss_token" = "$token" ]; then
		mptcp_lib_pr_ok
		mptcp_lib_result_pass "${msg}"
	else
		mptcp_lib_pr_fail "expected $ss_token found $token"
		mptcp_lib_result_fail "${msg}"
		ret=${KSFT_FAIL}
	fi
}

msk_info_get_value()
{
	local port="${1}"
	local info="${2}"

	ss -N "${ns}" -inHM dport "${port}" | \
		mptcp_lib_get_info_value "${info}" "${info}"
}

chk_msk_info()
{
	local port="${1}"
	local info="${2}"
	local cnt="${3}"
	local msg="....chk ${info}"
	local delta_ms=250  # half what we waited before, just to be sure
	local now

	now=$(msk_info_get_value "${port}" "${info}")

	mptcp_lib_print_title "${msg}"
	if { [ -z "${cnt}" ] || [ -z "${now}" ]; } &&
	   ! mptcp_lib_expect_all_features; then
		mptcp_lib_pr_skip "Feature probably not supported"
		mptcp_lib_result_skip "${msg}"
	elif [ "$((cnt + delta_ms))" -lt "${now}" ]; then
		mptcp_lib_pr_ok
		mptcp_lib_result_pass "${msg}"
	else
		mptcp_lib_pr_fail "value of ${info} changed by $((now - cnt))ms," \
				  "expected at least ${delta_ms}ms"
		mptcp_lib_result_fail "${msg}"
		ret=${KSFT_FAIL}
	fi
}

chk_last_time_info()
{
	local port="${1}"
	local data_sent data_recv ack_recv

	data_sent=$(msk_info_get_value "${port}" "last_data_sent")
	data_recv=$(msk_info_get_value "${port}" "last_data_recv")
	ack_recv=$(msk_info_get_value "${port}" "last_ack_recv")

	sleep 0.5  # wait to check after if the timestamps difference

	chk_msk_info "${port}" "last_data_sent" "${data_sent}"
	chk_msk_info "${port}" "last_data_recv" "${data_recv}"
	chk_msk_info "${port}" "last_ack_recv" "${ack_recv}"
}

wait_connected()
{
	local listener_ns="${1}"
	local port="${2}"

	local port_hex i

	port_hex="$(printf "%04X" "${port}")"
	for i in $(seq 10); do
		ip netns exec ${listener_ns} grep -q " 0100007F:${port_hex} " /proc/net/tcp && break
		sleep 0.1
	done
}

trap cleanup EXIT
mptcp_lib_ns_init ns

echo "a" | \
	timeout ${timeout_test} \
		ip netns exec $ns \
			./mptcp_connect -p 10000 -l -t ${timeout_poll} -w 20 \
				0.0.0.0 >/dev/null &
mptcp_lib_wait_local_port_listen $ns 10000
chk_msk_nr 0 "no msk on netns creation"
chk_msk_listen 10000

echo "b" | \
	timeout ${timeout_test} \
		ip netns exec $ns \
			./mptcp_connect -p 10000 -r 0 -t ${timeout_poll} -w 20 \
				127.0.0.1 >/dev/null &
wait_connected $ns 10000
chk_msk_nr 2 "after MPC handshake"
chk_last_time_info 10000
chk_msk_remote_key_nr 2 "....chk remote_key"
chk_msk_fallback_nr 0 "....chk no fallback"
chk_msk_inuse 2
chk_msk_cestab 2
chk_dump_one
flush_pids

chk_msk_inuse 0 "2->0"
chk_msk_cestab 0 "2->0"

echo "a" | \
	timeout ${timeout_test} \
		ip netns exec $ns \
			./mptcp_connect -p 10001 -l -s TCP -t ${timeout_poll} -w 20 \
				0.0.0.0 >/dev/null &
mptcp_lib_wait_local_port_listen $ns 10001
echo "b" | \
	timeout ${timeout_test} \
		ip netns exec $ns \
			./mptcp_connect -p 10001 -r 0 -t ${timeout_poll} -w 20 \
				127.0.0.1 >/dev/null &
wait_connected $ns 10001
chk_msk_fallback_nr 1 "check fallback"
chk_msk_inuse 1
chk_msk_cestab 1
flush_pids

chk_msk_inuse 0 "1->0"
chk_msk_cestab 0 "1->0"

NR_CLIENTS=100
for I in $(seq 1 $NR_CLIENTS); do
	echo "a" | \
		timeout ${timeout_test} \
			ip netns exec $ns \
				./mptcp_connect -p $((I+10001)) -l -w 20 \
					-t ${timeout_poll} 0.0.0.0 >/dev/null &
done
mptcp_lib_wait_local_port_listen $ns $((NR_CLIENTS + 10001))

for I in $(seq 1 $NR_CLIENTS); do
	echo "b" | \
		timeout ${timeout_test} \
			ip netns exec $ns \
				./mptcp_connect -p $((I+10001)) -w 20 \
					-t ${timeout_poll} 127.0.0.1 >/dev/null &
done

wait_msk_nr $((NR_CLIENTS*2)) "many msk socket present"
chk_msk_inuse $((NR_CLIENTS*2)) "many"
chk_msk_cestab $((NR_CLIENTS*2)) "many"
flush_pids

chk_msk_inuse 0 "many->0"
chk_msk_cestab 0 "many->0"

chk_listener_nr 0 "no listener sockets"
NR_SERVERS=100
for I in $(seq 1 $NR_SERVERS); do
	ip netns exec $ns ./mptcp_connect -p $((I + 20001)) \
		-t ${timeout_poll} -l 0.0.0.0 >/dev/null 2>&1 &
done
mptcp_lib_wait_local_port_listen $ns $((NR_SERVERS + 20001))

chk_listener_nr $NR_SERVERS "many listener sockets"

# graceful termination
for I in $(seq 1 $NR_SERVERS); do
	echo a | ip netns exec $ns ./mptcp_connect -p $((I + 20001)) 127.0.0.1 >/dev/null 2>&1 &
done
flush_pids

mptcp_lib_result_print_all_tap
exit $ret
