#!/bin/bash
#
# This tests connection tracking helper assignment:
# 1. can attach ftp helper to a connection from nft ruleset.
# 2. auto-assign still works.
#
# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4
ret=0

sfx=$(mktemp -u "XXXXXXXX")
ns1="ns1-$sfx"
ns2="ns2-$sfx"
testipv6=1

cleanup()
{
	ip netns del ${ns1}
	ip netns del ${ns2}
}

nft --version > /dev/null 2>&1
if [ $? -ne 0 ];then
	echo "SKIP: Could not run test without nft tool"
	exit $ksft_skip
fi

ip -Version > /dev/null 2>&1
if [ $? -ne 0 ];then
	echo "SKIP: Could not run test without ip tool"
	exit $ksft_skip
fi

conntrack -V > /dev/null 2>&1
if [ $? -ne 0 ];then
	echo "SKIP: Could not run test without conntrack tool"
	exit $ksft_skip
fi

which nc >/dev/null 2>&1
if [ $? -ne 0 ];then
	echo "SKIP: Could not run test without netcat tool"
	exit $ksft_skip
fi

trap cleanup EXIT

ip netns add ${ns1}
ip netns add ${ns2}

ip link add veth0 netns ${ns1} type veth peer name veth0 netns ${ns2} > /dev/null 2>&1
if [ $? -ne 0 ];then
    echo "SKIP: No virtual ethernet pair device support in kernel"
    exit $ksft_skip
fi

ip -net ${ns1} link set lo up
ip -net ${ns1} link set veth0 up

ip -net ${ns2} link set lo up
ip -net ${ns2} link set veth0 up

ip -net ${ns1} addr add 10.0.1.1/24 dev veth0
ip -net ${ns1} addr add dead:1::1/64 dev veth0

ip -net ${ns2} addr add 10.0.1.2/24 dev veth0
ip -net ${ns2} addr add dead:1::2/64 dev veth0

load_ruleset_family() {
	local family=$1
	local ns=$2

ip netns exec ${ns} nft -f - <<EOF
table $family raw {
	ct helper ftp {
             type "ftp" protocol tcp
        }
	chain pre {
		type filter hook prerouting priority 0; policy accept;
		tcp dport 2121 ct helper set "ftp"
	}
	chain output {
		type filter hook output priority 0; policy accept;
		tcp dport 2121 ct helper set "ftp"
	}
}
EOF
	return $?
}

check_for_helper()
{
	local netns=$1
	local message=$2
	local port=$3

	if echo $message |grep -q 'ipv6';then
		local family="ipv6"
	else
		local family="ipv4"
	fi

	ip netns exec ${netns} conntrack -L -f $family -p tcp --dport $port 2> /dev/null |grep -q 'helper=ftp'
	if [ $? -ne 0 ] ; then
		echo "FAIL: ${netns} did not show attached helper $message" 1>&2
		ret=1
	fi

	echo "PASS: ${netns} connection on port $port has ftp helper attached" 1>&2
	return 0
}

test_helper()
{
	local port=$1
	local msg=$2

	sleep 3 | ip netns exec ${ns2} nc -w 2 -l -p $port > /dev/null &

	sleep 1 | ip netns exec ${ns1} nc -w 2 10.0.1.2 $port > /dev/null &
	sleep 1

	check_for_helper "$ns1" "ip $msg" $port
	check_for_helper "$ns2" "ip $msg" $port

	wait

	if [ $testipv6 -eq 0 ] ;then
		return 0
	fi

	ip netns exec ${ns1} conntrack -F 2> /dev/null
	ip netns exec ${ns2} conntrack -F 2> /dev/null

	sleep 3 | ip netns exec ${ns2} nc -w 2 -6 -l -p $port > /dev/null &

	sleep 1 | ip netns exec ${ns1} nc -w 2 -6 dead:1::2 $port > /dev/null &
	sleep 1

	check_for_helper "$ns1" "ipv6 $msg" $port
	check_for_helper "$ns2" "ipv6 $msg" $port

	wait
}

load_ruleset_family ip ${ns1}
if [ $? -ne 0 ];then
	echo "FAIL: ${ns1} cannot load ip ruleset" 1>&2
	exit 1
fi

load_ruleset_family ip6 ${ns1}
if [ $? -ne 0 ];then
	echo "SKIP: ${ns1} cannot load ip6 ruleset" 1>&2
	testipv6=0
fi

load_ruleset_family inet ${ns2}
if [ $? -ne 0 ];then
	echo "SKIP: ${ns1} cannot load inet ruleset" 1>&2
	load_ruleset_family ip ${ns2}
	if [ $? -ne 0 ];then
		echo "FAIL: ${ns2} cannot load ip ruleset" 1>&2
		exit 1
	fi

	if [ $testipv6 -eq 1 ] ;then
		load_ruleset_family ip6 ${ns2}
		if [ $? -ne 0 ];then
			echo "FAIL: ${ns2} cannot load ip6 ruleset" 1>&2
			exit 1
		fi
	fi
fi

test_helper 2121 "set via ruleset"
ip netns exec ${ns1} sysctl -q 'net.netfilter.nf_conntrack_helper=1'
ip netns exec ${ns2} sysctl -q 'net.netfilter.nf_conntrack_helper=1'
test_helper 21 "auto-assign"

exit $ret
