#!/bin/bash
#
# This tests connection tracking helper assignment:
# 1. can attach ftp helper to a connection from nft ruleset.
# 2. auto-assign still works.
#
# Kselftest framework requirement - SKIP code is 4.

source lib.sh

ret=0

testipv6=1

checktool "socat -h" "run test without socat"
checktool "conntrack --version" "run test without conntrack"
checktool "nft --version" "run test without nft"

cleanup()
{
	ip netns pids "$ns1" | xargs kill 2>/dev/null

	ip netns del "$ns1"
	ip netns del "$ns2"
}

trap cleanup EXIT

setup_ns ns1 ns2

if ! ip link add veth0 netns "$ns1" type veth peer name veth0 netns "$ns2" > /dev/null 2>&1;then
    echo "SKIP: No virtual ethernet pair device support in kernel"
    exit $ksft_skip
fi

ip -net "$ns1" link set veth0 up
ip -net "$ns2" link set veth0 up

ip -net "$ns1" addr add 10.0.1.1/24 dev veth0
ip -net "$ns1" addr add dead:1::1/64 dev veth0 nodad

ip -net "$ns2" addr add 10.0.1.2/24 dev veth0
ip -net "$ns2" addr add dead:1::2/64 dev veth0 nodad

load_ruleset_family() {
	local family=$1
	local ns=$2

ip netns exec "$ns" nft -f - <<EOF
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

	if echo "$message" |grep -q 'ipv6';then
		local family="ipv6"
	else
		local family="ipv4"
	fi

	if ! ip netns exec "$netns" conntrack -L -f $family -p tcp --dport "$port" 2> /dev/null |grep -q 'helper=ftp';then
		if [ "$autoassign" -eq 0 ] ;then
			echo "FAIL: ${netns} did not show attached helper $message" 1>&2
			ret=1
		else
			echo "PASS: ${netns} did not show attached helper $message" 1>&2
		fi
	else
		if [ "$autoassign" -eq 0 ] ;then
			echo "PASS: ${netns} connection on port $port has ftp helper attached" 1>&2
		else
			echo "FAIL: ${netns} connection on port $port has ftp helper attached" 1>&2
			ret=1
		fi
	fi

	return 0
}

listener_ready()
{
	ns="$1"
	port="$2"
	proto="$3"
	ss -N "$ns" -lnt -o "sport = :$port" | grep -q "$port"
}

test_helper()
{
	local port=$1
	local autoassign=$2

	if [ "$autoassign" -eq 0 ] ;then
		msg="set via ruleset"
	else
		msg="auto-assign"
	fi

	ip netns exec "$ns2" socat -t 3 -u -4 TCP-LISTEN:"$port",reuseaddr STDOUT > /dev/null &
	busywait "$BUSYWAIT_TIMEOUT" listener_ready "$ns2" "$port" "-4"

	ip netns exec "$ns1" socat -u -4 STDIN TCP:10.0.1.2:"$port" < /dev/null > /dev/null

	check_for_helper "$ns1" "ip $msg" "$port" "$autoassign"
	check_for_helper "$ns2" "ip $msg" "$port" "$autoassign"

	if [ $testipv6 -eq 0 ] ;then
		return 0
	fi

	ip netns exec "$ns1" conntrack -F 2> /dev/null
	ip netns exec "$ns2" conntrack -F 2> /dev/null

	ip netns exec "$ns2" socat -t 3 -u -6 TCP-LISTEN:"$port",reuseaddr STDOUT > /dev/null &
	busywait $BUSYWAIT_TIMEOUT listener_ready "$ns2" "$port" "-6"

	ip netns exec "$ns1" socat -t 3 -u -6 STDIN TCP:"[dead:1::2]":"$port" < /dev/null > /dev/null

	check_for_helper "$ns1" "ipv6 $msg" "$port"
	check_for_helper "$ns2" "ipv6 $msg" "$port"
}

if ! load_ruleset_family ip "$ns1"; then
	echo "FAIL: ${ns1} cannot load ip ruleset" 1>&2
	exit 1
fi

if ! load_ruleset_family ip6 "$ns1"; then
	echo "SKIP: ${ns1} cannot load ip6 ruleset" 1>&2
	testipv6=0
fi

if ! load_ruleset_family inet "${ns2}"; then
	echo "SKIP: ${ns1} cannot load inet ruleset" 1>&2
	if ! load_ruleset_family ip "${ns2}"; then
		echo "FAIL: ${ns2} cannot load ip ruleset" 1>&2
		exit 1
	fi

	if [ "$testipv6" -eq 1 ] ;then
		if ! load_ruleset_family ip6 "$ns2"; then
			echo "FAIL: ${ns2} cannot load ip6 ruleset" 1>&2
			exit 1
		fi
	fi
fi

test_helper 2121 0
ip netns exec "$ns1" sysctl -qe 'net.netfilter.nf_conntrack_helper=1'
ip netns exec "$ns2" sysctl -qe 'net.netfilter.nf_conntrack_helper=1'
test_helper 21 1

exit $ret
