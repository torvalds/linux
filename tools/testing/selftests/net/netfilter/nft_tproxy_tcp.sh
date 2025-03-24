#!/bin/bash
#
# This tests tproxy on the following scenario:
#
#                         +------------+
# +-------+               |  nsrouter  |                  +-------+
# |ns1    |.99          .1|            |.1             .99|    ns2|
# |   eth0|---------------|veth0  veth1|------------------|eth0   |
# |       |  10.0.1.0/24  |            |   10.0.2.0/24    |       |
# +-------+  dead:1::/64  |    veth2   |   dead:2::/64    +-------+
#                         +------------+
#                                |.1
#                                |
#                                |
#                                |                        +-------+
#                                |                     .99|    ns3|
#                                +------------------------|eth0   |
#                                       10.0.3.0/24       |       |
#                                       dead:3::/64       +-------+
#
# The tproxy implementation acts as an echo server so the client
# must receive the same message it sent if it has been proxied.
# If is not proxied the servers return PONG_NS# with the number
# of the namespace the server is running.
#
# shellcheck disable=SC2162,SC2317

source lib.sh
ret=0
timeout=5

cleanup()
{
	ip netns pids "$ns1" | xargs kill 2>/dev/null
	ip netns pids "$ns2" | xargs kill 2>/dev/null
	ip netns pids "$ns3" | xargs kill 2>/dev/null
	ip netns pids "$nsrouter" | xargs kill 2>/dev/null

	cleanup_all_ns
}

checktool "nft --version" "test without nft tool"
checktool "socat -h" "run test without socat"

trap cleanup EXIT
setup_ns ns1 ns2 ns3 nsrouter

if ! ip link add veth0 netns "$nsrouter" type veth peer name eth0 netns "$ns1" > /dev/null 2>&1; then
    echo "SKIP: No virtual ethernet pair device support in kernel"
    exit $ksft_skip
fi
ip link add veth1 netns "$nsrouter" type veth peer name eth0 netns "$ns2"
ip link add veth2 netns "$nsrouter" type veth peer name eth0 netns "$ns3"

ip -net "$nsrouter" link set veth0 up
ip -net "$nsrouter" addr add 10.0.1.1/24 dev veth0
ip -net "$nsrouter" addr add dead:1::1/64 dev veth0 nodad

ip -net "$nsrouter" link set veth1 up
ip -net "$nsrouter" addr add 10.0.2.1/24 dev veth1
ip -net "$nsrouter" addr add dead:2::1/64 dev veth1 nodad

ip -net "$nsrouter" link set veth2 up
ip -net "$nsrouter" addr add 10.0.3.1/24 dev veth2
ip -net "$nsrouter" addr add dead:3::1/64 dev veth2 nodad

ip -net "$ns1" link set eth0 up
ip -net "$ns2" link set eth0 up
ip -net "$ns3" link set eth0 up

ip -net "$ns1" addr add 10.0.1.99/24 dev eth0
ip -net "$ns1" addr add dead:1::99/64 dev eth0 nodad
ip -net "$ns1" route add default via 10.0.1.1
ip -net "$ns1" route add default via dead:1::1

ip -net "$ns2" addr add 10.0.2.99/24 dev eth0
ip -net "$ns2" addr add dead:2::99/64 dev eth0 nodad
ip -net "$ns2" route add default via 10.0.2.1
ip -net "$ns2" route add default via dead:2::1

ip -net "$ns3" addr add 10.0.3.99/24 dev eth0
ip -net "$ns3" addr add dead:3::99/64 dev eth0 nodad
ip -net "$ns3" route add default via 10.0.3.1
ip -net "$ns3" route add default via dead:3::1

ip netns exec "$nsrouter" sysctl net.ipv6.conf.all.forwarding=1 > /dev/null
ip netns exec "$nsrouter" sysctl net.ipv4.conf.veth0.forwarding=1 > /dev/null
ip netns exec "$nsrouter" sysctl net.ipv4.conf.veth1.forwarding=1 > /dev/null
ip netns exec "$nsrouter" sysctl net.ipv4.conf.veth2.forwarding=1 > /dev/null

test_ping() {
  if ! ip netns exec "$ns1" ping -c 1 -q 10.0.2.99 > /dev/null; then
	return 1
  fi

  if ! ip netns exec "$ns1" ping -c 1 -q dead:2::99 > /dev/null; then
	return 2
  fi

  if ! ip netns exec "$ns1" ping -c 1 -q 10.0.3.99 > /dev/null; then
	return 1
  fi

  if ! ip netns exec "$ns1" ping -c 1 -q dead:3::99 > /dev/null; then
	return 2
  fi

  return 0
}

test_ping_router() {
  if ! ip netns exec "$ns1" ping -c 1 -q 10.0.2.1 > /dev/null; then
	return 3
  fi

  if ! ip netns exec "$ns1" ping -c 1 -q dead:2::1 > /dev/null; then
	return 4
  fi

  return 0
}


listener_ready()
{
	local ns="$1"
	local port="$2"
	local proto="$3"
	ss -N "$ns" -ln "$proto" -o "sport = :$port" | grep -q "$port"
}

test_tproxy()
{
	local traffic_origin="$1"
	local ip_proto="$2"
	local expect_ns1_ns2="$3"
	local expect_ns1_ns3="$4"
	local expect_nsrouter_ns2="$5"
	local expect_nsrouter_ns3="$6"

	# derived variables
	local testname="test_${ip_proto}_tcp_${traffic_origin}"
	local socat_ipproto
	local ns1_ip
	local ns2_ip
	local ns3_ip
	local ns2_target
	local ns3_target
	local nftables_subject
	local ip_command

	# socat 1.8.0 has a bug that requires to specify the IP family to bind (fixed in 1.8.0.1)
	case $ip_proto in
	"ip")
		socat_ipproto="-4"
		ns1_ip=10.0.1.99
		ns2_ip=10.0.2.99
		ns3_ip=10.0.3.99
		ns2_target="tcp:$ns2_ip:8080"
		ns3_target="tcp:$ns3_ip:8080"
		nftables_subject="ip daddr $ns2_ip tcp dport 8080"
		ip_command="ip"
	;;
	"ip6")
		socat_ipproto="-6"
		ns1_ip=dead:1::99
		ns2_ip=dead:2::99
		ns3_ip=dead:3::99
		ns2_target="tcp:[$ns2_ip]:8080"
		ns3_target="tcp:[$ns3_ip]:8080"
		nftables_subject="ip6 daddr $ns2_ip tcp dport 8080"
		ip_command="ip -6"
	;;
	*)
	echo "FAIL: unsupported protocol"
	exit 255
	;;
	esac

	case $traffic_origin in
	# to capture the local originated traffic we need to mark the outgoing
	# traffic so the policy based routing rule redirects it and can be processed
	# in the prerouting chain.
	"local")
		nftables_rules="
flush ruleset
table inet filter {
	chain divert {
		type filter hook prerouting priority 0; policy accept;
		$nftables_subject tproxy $ip_proto to :12345 meta mark set 1 accept
	}
	chain output {
		type route hook output priority 0; policy accept;
		$nftables_subject meta mark set 1 accept
	}
}"
	;;
	"forward")
		nftables_rules="
flush ruleset
table inet filter {
	chain divert {
		type filter hook prerouting priority 0; policy accept;
		$nftables_subject tproxy $ip_proto to :12345 meta mark set 1 accept
	}
}"
	;;
	*)
	echo "FAIL: unsupported parameter for traffic origin"
	exit 255
	;;
	esac

	# shellcheck disable=SC2046 # Intended splitting of ip_command
	ip netns exec "$nsrouter" $ip_command rule add fwmark 1 table 100
	ip netns exec "$nsrouter" $ip_command route add local "${ns2_ip}" dev lo table 100
	echo "$nftables_rules" | ip netns exec "$nsrouter" nft -f /dev/stdin

	timeout "$timeout" ip netns exec "$nsrouter" socat "$socat_ipproto" tcp-listen:12345,fork,ip-transparent SYSTEM:"cat" 2>/dev/null &
	local tproxy_pid=$!

	timeout "$timeout" ip netns exec "$ns2" socat "$socat_ipproto" tcp-listen:8080,fork SYSTEM:"echo PONG_NS2" 2>/dev/null &
	local server2_pid=$!

	timeout "$timeout" ip netns exec "$ns3" socat "$socat_ipproto" tcp-listen:8080,fork SYSTEM:"echo PONG_NS3" 2>/dev/null &
	local server3_pid=$!

	busywait "$BUSYWAIT_TIMEOUT" listener_ready "$nsrouter" 12345 "-t"
	busywait "$BUSYWAIT_TIMEOUT" listener_ready "$ns2" 8080 "-t"
	busywait "$BUSYWAIT_TIMEOUT" listener_ready "$ns3" 8080 "-t"

	local result
	# request from ns1 to ns2 (forwarded traffic)
	result=$(echo I_M_PROXIED | ip netns exec "$ns1" socat -t 2 -T 2 STDIO "$ns2_target")
	if [ "$result" == "$expect_ns1_ns2" ] ;then
		echo "PASS: tproxy test $testname: ns1 got reply \"$result\" connecting to ns2"
	else
		echo "ERROR: tproxy test $testname: ns1 got reply \"$result\" connecting to ns2, not \"${expect_ns1_ns2}\" as intended"
		ret=1
	fi

	# request from ns1 to ns3(forwarded traffic)
	result=$(echo I_M_PROXIED | ip netns exec "$ns1" socat -t 2 -T 2 STDIO "$ns3_target")
	if [ "$result" = "$expect_ns1_ns3" ] ;then
		echo "PASS: tproxy test $testname: ns1 got reply \"$result\" connecting to ns3"
	else
		echo "ERROR: tproxy test $testname: ns1 got reply \"$result\" connecting to ns3, not \"$expect_ns1_ns3\" as intended"
		ret=1
	fi

	# request from nsrouter to ns2 (localy originated traffic)
	result=$(echo I_M_PROXIED | ip netns exec "$nsrouter" socat -t 2 -T 2 STDIO "$ns2_target")
	if [ "$result" == "$expect_nsrouter_ns2" ] ;then
		echo "PASS: tproxy test $testname: nsrouter got reply \"$result\" connecting to ns2"
	else
		echo "ERROR: tproxy test $testname: nsrouter got reply \"$result\" connecting to ns2, not \"$expect_nsrouter_ns2\" as intended"
		ret=1
	fi

	# request from nsrouter to ns3 (localy originated traffic)
	result=$(echo I_M_PROXIED | ip netns exec "$nsrouter" socat -t 2 -T 2 STDIO "$ns3_target")
	if [ "$result" = "$expect_nsrouter_ns3" ] ;then
		echo "PASS: tproxy test $testname: nsrouter got reply \"$result\" connecting to ns3"
	else
		echo "ERROR: tproxy test $testname: nsrouter got reply \"$result\" connecting to ns3, not \"$expect_nsrouter_ns3\"  as intended"
		ret=1
	fi

	# cleanup
	kill "$tproxy_pid" "$server2_pid" "$server3_pid" 2>/dev/null
	# shellcheck disable=SC2046 # Intended splitting of ip_command
	ip netns exec "$nsrouter" $ip_command rule del fwmark 1 table 100
	ip netns exec "$nsrouter" $ip_command route flush table 100
}


test_ipv4_tcp_forward()
{
	local traffic_origin="forward"
	local ip_proto="ip"
	local expect_ns1_ns2="I_M_PROXIED"
	local expect_ns1_ns3="PONG_NS3"
	local expect_nsrouter_ns2="PONG_NS2"
	local expect_nsrouter_ns3="PONG_NS3"

	test_tproxy     "$traffic_origin" \
			"$ip_proto" \
			"$expect_ns1_ns2" \
			"$expect_ns1_ns3" \
			"$expect_nsrouter_ns2" \
			"$expect_nsrouter_ns3"
}

test_ipv4_tcp_local()
{
	local traffic_origin="local"
	local ip_proto="ip"
	local expect_ns1_ns2="I_M_PROXIED"
	local expect_ns1_ns3="PONG_NS3"
	local expect_nsrouter_ns2="I_M_PROXIED"
	local expect_nsrouter_ns3="PONG_NS3"

	test_tproxy     "$traffic_origin" \
			"$ip_proto" \
			"$expect_ns1_ns2" \
			"$expect_ns1_ns3" \
			"$expect_nsrouter_ns2" \
			"$expect_nsrouter_ns3"
}

test_ipv6_tcp_forward()
{
	local traffic_origin="forward"
	local ip_proto="ip6"
	local expect_ns1_ns2="I_M_PROXIED"
	local expect_ns1_ns3="PONG_NS3"
	local expect_nsrouter_ns2="PONG_NS2"
	local expect_nsrouter_ns3="PONG_NS3"

	test_tproxy     "$traffic_origin" \
			"$ip_proto" \
			"$expect_ns1_ns2" \
			"$expect_ns1_ns3" \
			"$expect_nsrouter_ns2" \
			"$expect_nsrouter_ns3"
}

test_ipv6_tcp_local()
{
	local traffic_origin="local"
	local ip_proto="ip6"
	local expect_ns1_ns2="I_M_PROXIED"
	local expect_ns1_ns3="PONG_NS3"
	local expect_nsrouter_ns2="I_M_PROXIED"
	local expect_nsrouter_ns3="PONG_NS3"

	test_tproxy     "$traffic_origin" \
			"$ip_proto" \
			"$expect_ns1_ns2" \
			"$expect_ns1_ns3" \
			"$expect_nsrouter_ns2" \
			"$expect_nsrouter_ns3"
}

if test_ping; then
	# queue bypass works (rules were skipped, no listener)
	echo "PASS: ${ns1} can reach ${ns2}"
else
	echo "FAIL: ${ns1} cannot reach ${ns2}: $ret" 1>&2
	exit $ret
fi

test_ipv4_tcp_forward
test_ipv4_tcp_local
test_ipv6_tcp_forward
test_ipv6_tcp_local

exit $ret
