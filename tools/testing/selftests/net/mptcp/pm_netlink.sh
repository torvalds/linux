#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

. "$(dirname "${0}")/mptcp_lib.sh"

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
	rm -f $err
	ip netns del $ns1
}

mptcp_lib_check_mptcp

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

	printf "%-50s" "$msg"
	if [ $cmd_ret -ne 0 ]; then
		echo "[FAIL] command execution '$cmd' stderr "
		cat $err
		mptcp_lib_result_fail "${msg} # error ${cmd_ret}"
		ret=1
	elif [ "$out" = "$expected" ]; then
		echo "[ OK ]"
		mptcp_lib_result_pass "${msg}"
	else
		echo -n "[FAIL] "
		echo "expected '$expected' got '$out'"
		mptcp_lib_result_fail "${msg} # different output"
		ret=1
	fi
}

check "ip netns exec $ns1 ./pm_nl_ctl dump" "" "defaults addr list"

default_limits="$(ip netns exec $ns1 ./pm_nl_ctl limits)"
if mptcp_lib_expect_all_features; then
	check "ip netns exec $ns1 ./pm_nl_ctl limits" "accept 0
subflows 2" "defaults limits"
fi

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

ip netns exec $ns1 ./pm_nl_ctl add 10.0.1.3 2>/dev/null
check "ip netns exec $ns1 ./pm_nl_ctl get 4" "" "duplicate addr"

ip netns exec $ns1 ./pm_nl_ctl add 10.0.1.4 flags signal
check "ip netns exec $ns1 ./pm_nl_ctl get 4" "id 4 flags signal 10.0.1.4" "id addr increment"

for i in `seq 5 9`; do
	ip netns exec $ns1 ./pm_nl_ctl add 10.0.1.$i flags signal >/dev/null 2>&1
done
check "ip netns exec $ns1 ./pm_nl_ctl get 9" "id 9 flags signal 10.0.1.9" "hard addr limit"
check "ip netns exec $ns1 ./pm_nl_ctl get 10" "" "above hard addr limit"

ip netns exec $ns1 ./pm_nl_ctl del 9
for i in `seq 10 255`; do
	ip netns exec $ns1 ./pm_nl_ctl add 10.0.0.9 id $i
	ip netns exec $ns1 ./pm_nl_ctl del $i
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

ip netns exec $ns1 ./pm_nl_ctl limits 9 1 2>/dev/null
check "ip netns exec $ns1 ./pm_nl_ctl limits" "$default_limits" "rcv addrs above hard limit"

ip netns exec $ns1 ./pm_nl_ctl limits 1 9 2>/dev/null
check "ip netns exec $ns1 ./pm_nl_ctl limits" "$default_limits" "subflows above hard limit"

ip netns exec $ns1 ./pm_nl_ctl limits 8 8
check "ip netns exec $ns1 ./pm_nl_ctl limits" "accept 8
subflows 8" "set limits"

ip netns exec $ns1 ./pm_nl_ctl flush
ip netns exec $ns1 ./pm_nl_ctl add 10.0.1.1
ip netns exec $ns1 ./pm_nl_ctl add 10.0.1.2
ip netns exec $ns1 ./pm_nl_ctl add 10.0.1.3 id 100
ip netns exec $ns1 ./pm_nl_ctl add 10.0.1.4
ip netns exec $ns1 ./pm_nl_ctl add 10.0.1.5 id 254
ip netns exec $ns1 ./pm_nl_ctl add 10.0.1.6
ip netns exec $ns1 ./pm_nl_ctl add 10.0.1.7
ip netns exec $ns1 ./pm_nl_ctl add 10.0.1.8
check "ip netns exec $ns1 ./pm_nl_ctl dump" "id 1 flags  10.0.1.1
id 2 flags  10.0.1.2
id 3 flags  10.0.1.7
id 4 flags  10.0.1.8
id 100 flags  10.0.1.3
id 101 flags  10.0.1.4
id 254 flags  10.0.1.5
id 255 flags  10.0.1.6" "set ids"

ip netns exec $ns1 ./pm_nl_ctl flush
ip netns exec $ns1 ./pm_nl_ctl add 10.0.0.1
ip netns exec $ns1 ./pm_nl_ctl add 10.0.0.2 id 254
ip netns exec $ns1 ./pm_nl_ctl add 10.0.0.3
ip netns exec $ns1 ./pm_nl_ctl add 10.0.0.4
ip netns exec $ns1 ./pm_nl_ctl add 10.0.0.5 id 253
ip netns exec $ns1 ./pm_nl_ctl add 10.0.0.6
ip netns exec $ns1 ./pm_nl_ctl add 10.0.0.7
ip netns exec $ns1 ./pm_nl_ctl add 10.0.0.8
check "ip netns exec $ns1 ./pm_nl_ctl dump" "id 1 flags  10.0.0.1
id 2 flags  10.0.0.4
id 3 flags  10.0.0.6
id 4 flags  10.0.0.7
id 5 flags  10.0.0.8
id 253 flags  10.0.0.5
id 254 flags  10.0.0.2
id 255 flags  10.0.0.3" "wrap-around ids"

ip netns exec $ns1 ./pm_nl_ctl flush
ip netns exec $ns1 ./pm_nl_ctl add 10.0.1.1 flags subflow
ip netns exec $ns1 ./pm_nl_ctl set 10.0.1.1 flags backup
check "ip netns exec $ns1 ./pm_nl_ctl dump" "id 1 flags \
subflow,backup 10.0.1.1" "set flags (backup)"
ip netns exec $ns1 ./pm_nl_ctl set 10.0.1.1 flags nobackup
check "ip netns exec $ns1 ./pm_nl_ctl dump" "id 1 flags \
subflow 10.0.1.1" "          (nobackup)"

# fullmesh support has been added later
ip netns exec $ns1 ./pm_nl_ctl set id 1 flags fullmesh
if ip netns exec $ns1 ./pm_nl_ctl dump | grep -q "fullmesh" ||
   mptcp_lib_expect_all_features; then
	check "ip netns exec $ns1 ./pm_nl_ctl dump" "id 1 flags \
subflow,fullmesh 10.0.1.1" "          (fullmesh)"
	ip netns exec $ns1 ./pm_nl_ctl set id 1 flags nofullmesh
	check "ip netns exec $ns1 ./pm_nl_ctl dump" "id 1 flags \
subflow 10.0.1.1" "          (nofullmesh)"
	ip netns exec $ns1 ./pm_nl_ctl set id 1 flags backup,fullmesh
	check "ip netns exec $ns1 ./pm_nl_ctl dump" "id 1 flags \
subflow,backup,fullmesh 10.0.1.1" "          (backup,fullmesh)"
fi

mptcp_lib_result_print_all_tap
exit $ret
