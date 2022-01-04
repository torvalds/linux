#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

rndh=$(printf %x $sec)-$(mktemp -u XXXXXX)
ns="ns1-$rndh"
ksft_skip=4
test_cnt=1
timeout_poll=100
timeout_test=$((timeout_poll * 2 + 1))
ret=0

flush_pids()
{
	# mptcp_connect in join mode will sleep a bit before completing,
	# give it some time
	sleep 1.1

	ip netns pids "${ns}" | xargs --no-run-if-empty kill -SIGUSR1 &>/dev/null
}

cleanup()
{
	ip netns pids "${ns}" | xargs --no-run-if-empty kill -SIGKILL &>/dev/null

	ip netns del $ns
}

ip -Version > /dev/null 2>&1
if [ $? -ne 0 ];then
	echo "SKIP: Could not run test without ip tool"
	exit $ksft_skip
fi
ss -h | grep -q MPTCP
if [ $? -ne 0 ];then
	echo "SKIP: ss tool does not support MPTCP"
	exit $ksft_skip
fi

__chk_nr()
{
	local condition="$1"
	local expected=$2
	local msg nr

	shift 2
	msg=$*
	nr=$(ss -inmHMN $ns | $condition)

	printf "%-50s" "$msg"
	if [ $nr != $expected ]; then
		echo "[ fail ] expected $expected found $nr"
		ret=$test_cnt
	else
		echo "[  ok  ]"
	fi
	test_cnt=$((test_cnt+1))
}

chk_msk_nr()
{
	__chk_nr "grep -c token:" $*
}

chk_msk_fallback_nr()
{
		__chk_nr "grep -c fallback" $*
}

chk_msk_remote_key_nr()
{
		__chk_nr "grep -c remote_key" $*
}


trap cleanup EXIT
ip netns add $ns
ip -n $ns link set dev lo up

echo "a" | \
	timeout ${timeout_test} \
		ip netns exec $ns \
			./mptcp_connect -p 10000 -l -t ${timeout_poll} \
				0.0.0.0 >/dev/null &
sleep 0.1
chk_msk_nr 0 "no msk on netns creation"

echo "b" | \
	timeout ${timeout_test} \
		ip netns exec $ns \
			./mptcp_connect -p 10000 -j -t ${timeout_poll} \
				127.0.0.1 >/dev/null &
sleep 0.1
chk_msk_nr 2 "after MPC handshake "
chk_msk_remote_key_nr 2 "....chk remote_key"
chk_msk_fallback_nr 0 "....chk no fallback"
flush_pids


echo "a" | \
	timeout ${timeout_test} \
		ip netns exec $ns \
			./mptcp_connect -p 10001 -l -s TCP -t ${timeout_poll} \
				0.0.0.0 >/dev/null &
sleep 0.1
echo "b" | \
	timeout ${timeout_test} \
		ip netns exec $ns \
			./mptcp_connect -p 10001 -j -t ${timeout_poll} \
				127.0.0.1 >/dev/null &
sleep 0.1
chk_msk_fallback_nr 1 "check fallback"
flush_pids

NR_CLIENTS=100
for I in `seq 1 $NR_CLIENTS`; do
	echo "a" | \
		timeout ${timeout_test} \
			ip netns exec $ns \
				./mptcp_connect -p $((I+10001)) -l -w 10 \
					-t ${timeout_poll} 0.0.0.0 >/dev/null &
done
sleep 0.1

for I in `seq 1 $NR_CLIENTS`; do
	echo "b" | \
		timeout ${timeout_test} \
			ip netns exec $ns \
				./mptcp_connect -p $((I+10001)) -w 10 \
					-t ${timeout_poll} 127.0.0.1 >/dev/null &
done
sleep 1.5

chk_msk_nr $((NR_CLIENTS*2)) "many msk socket present"
flush_pids

exit $ret
