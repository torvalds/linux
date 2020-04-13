#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

ksft_skip=4
ret=0

usage() {
	echo "Usage: $0 [ -h ]"
}


while getopts "$optstring" option;do
	case "$option" in
	"h")
		usage $0
		exit 0
		;;
	"?")
		usage $0
		exit 1
		;;
	esac
done

sec=$(date +%s)
rndh=$(printf %x $sec)-$(mktemp -u XXXXXX)
ns1="ns1-$rndh"
err=$(mktemp)
ret=0

cleanup()
{
	rm -f $out
	ip netns del $ns1
}

ip -Version > /dev/null 2>&1
if [ $? -ne 0 ];then
	echo "SKIP: Could not run test without ip tool"
	exit $ksft_skip
fi

trap cleanup EXIT

ip netns add $ns1 || exit $ksft_skip
ip -net $ns1 link set lo up
ip netns exec $ns1 sysctl -q net.mptcp.enabled=1

check()
{
	local cmd="$1"
	local expected="$2"
	local msg="$3"
	local out=`$cmd 2>$err`
	local cmd_ret=$?

	printf "%-50s %s" "$msg"
	if [ $cmd_ret -ne 0 ]; then
		echo "[FAIL] command execution '$cmd' stderr "
		cat $err
		ret=1
	elif [ "$out" = "$expected" ]; then
		echo "[ OK ]"
	else
		echo -n "[FAIL] "
		echo "expected '$expected' got '$out'"
		ret=1
	fi
}

check "ip netns exec $ns1 ./pm_nl_ctl dump" "" "defaults addr list"
check "ip netns exec $ns1 ./pm_nl_ctl limits" "accept 0
subflows 0" "defaults limits"

ip netns exec $ns1 ./pm_nl_ctl add 10.0.1.1
ip netns exec $ns1 ./pm_nl_ctl add 10.0.1.2 flags subflow dev lo
ip netns exec $ns1 ./pm_nl_ctl add 10.0.1.3 flags signal,backup
check "ip netns exec $ns1 ./pm_nl_ctl get 1" "id 1 flags  10.0.1.1" "simple add/get addr"

check "ip netns exec $ns1 ./pm_nl_ctl dump" \
"id 1 flags  10.0.1.1
id 2 flags subflow dev lo 10.0.1.2
id 3 flags signal,backup 10.0.1.3" "dump addrs"

ip netns exec $ns1 ./pm_nl_ctl del 2
check "ip netns exec $ns1 ./pm_nl_ctl get 2" "" "simple del addr"
check "ip netns exec $ns1 ./pm_nl_ctl dump" \
"id 1 flags  10.0.1.1
id 3 flags signal,backup 10.0.1.3" "dump addrs after del"

ip netns exec $ns1 ./pm_nl_ctl add 10.0.1.3
check "ip netns exec $ns1 ./pm_nl_ctl get 4" "" "duplicate addr"

ip netns exec $ns1 ./pm_nl_ctl add 10.0.1.4 id 10 flags signal
check "ip netns exec $ns1 ./pm_nl_ctl get 4" "id 4 flags signal 10.0.1.4" "id addr increment"

for i in `seq 5 9`; do
	ip netns exec $ns1 ./pm_nl_ctl add 10.0.1.$i flags signal >/dev/null 2>&1
done
check "ip netns exec $ns1 ./pm_nl_ctl get 9" "id 9 flags signal 10.0.1.9" "hard addr limit"
check "ip netns exec $ns1 ./pm_nl_ctl get 10" "" "above hard addr limit"

for i in `seq 9 256`; do
	ip netns exec $ns1 ./pm_nl_ctl del $i
	ip netns exec $ns1 ./pm_nl_ctl add 10.0.0.9
done
check "ip netns exec $ns1 ./pm_nl_ctl dump" "id 1 flags  10.0.1.1
id 3 flags signal,backup 10.0.1.3
id 4 flags signal 10.0.1.4
id 5 flags signal 10.0.1.5
id 6 flags signal 10.0.1.6
id 7 flags signal 10.0.1.7
id 8 flags signal 10.0.1.8" "id limit"

ip netns exec $ns1 ./pm_nl_ctl flush
check "ip netns exec $ns1 ./pm_nl_ctl dump" "" "flush addrs"

ip netns exec $ns1 ./pm_nl_ctl limits 9 1
check "ip netns exec $ns1 ./pm_nl_ctl limits" "accept 0
subflows 0" "rcv addrs above hard limit"

ip netns exec $ns1 ./pm_nl_ctl limits 1 9
check "ip netns exec $ns1 ./pm_nl_ctl limits" "accept 0
subflows 0" "subflows above hard limit"

ip netns exec $ns1 ./pm_nl_ctl limits 8 8
check "ip netns exec $ns1 ./pm_nl_ctl limits" "accept 8
subflows 8" "set limits"

exit $ret
