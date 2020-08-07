#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

rndh=$(printf %x $sec)-$(mktemp -u XXXXXX)
ns="ns1-$rndh"
ksft_skip=4
test_cnt=1
ret=0
pids=()

flush_pids()
{
	# mptcp_connect in join mode will sleep a bit before completing,
	# give it some time
	sleep 1.1

	for pid in ${pids[@]}; do
		[ -d /proc/$pid ] && kill -SIGUSR1 $pid >/dev/null 2>&1
	done
	pids=()
}

cleanup()
{
	ip netns del $ns
	for pid in ${pids[@]}; do
		[ -d /proc/$pid ] && kill -9 $pid >/dev/null 2>&1
	done
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

echo "a" | ip netns exec $ns ./mptcp_connect -p 10000 -l 0.0.0.0 -t 100 >/dev/null &
sleep 0.1
pids[0]=$!
chk_msk_nr 0 "no msk on netns creation"

echo "b" | ip netns exec $ns ./mptcp_connect -p 10000 127.0.0.1 -j -t 100 >/dev/null &
sleep 0.1
pids[1]=$!
chk_msk_nr 2 "after MPC handshake "
chk_msk_remote_key_nr 2 "....chk remote_key"
chk_msk_fallback_nr 0 "....chk no fallback"
flush_pids


echo "a" | ip netns exec $ns ./mptcp_connect -p 10001 -s TCP -l 0.0.0.0 -t 100 >/dev/null &
pids[0]=$!
sleep 0.1
echo "b" | ip netns exec $ns ./mptcp_connect -p 10001 127.0.0.1 -j -t 100 >/dev/null &
pids[1]=$!
sleep 0.1
chk_msk_fallback_nr 1 "check fallback"
flush_pids

NR_CLIENTS=100
for I in `seq 1 $NR_CLIENTS`; do
	echo "a" | ip netns exec $ns ./mptcp_connect -p $((I+10001)) -l 0.0.0.0 -t 100 -w 10 >/dev/null  &
	pids[$((I*2))]=$!
done
sleep 0.1

for I in `seq 1 $NR_CLIENTS`; do
	echo "b" | ip netns exec $ns ./mptcp_connect -p $((I+10001)) 127.0.0.1 -t 100 -w 10 >/dev/null &
	pids[$((I*2 + 1))]=$!
done
sleep 1.5

chk_msk_nr $((NR_CLIENTS*2)) "many msk socket present"
flush_pids

exit $ret
