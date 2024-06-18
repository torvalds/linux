#!/bin/bash
#
# This test is for bridge 'brouting', i.e. make some packets being routed
# rather than getting bridged even though they arrive on interface that is
# part of a bridge.

#           eth0    br0     eth0
# setup is: ns1 <-> nsbr <-> ns2

source lib.sh

if ! ebtables -V > /dev/null 2>&1;then
	echo "SKIP: Could not run test without ebtables"
	exit $ksft_skip
fi

cleanup() {
	cleanup_all_ns
}

trap cleanup EXIT

setup_ns nsbr ns1 ns2

ip netns exec "$nsbr" sysctl -q net.ipv4.conf.default.rp_filter=0
ip netns exec "$nsbr" sysctl -q net.ipv4.conf.all.rp_filter=0
if ! ip link add veth0 netns "$nsbr" type veth peer name eth0 netns "$ns1"; then
	echo "SKIP: Can't create veth device"
	exit $ksft_skip
fi
ip link add veth1 netns "$nsbr" type veth peer name eth0 netns "$ns2"

if ! ip -net "$nsbr" link add br0 type bridge; then
	echo "SKIP: Can't create bridge br0"
	exit $ksft_skip
fi

ip -net "$nsbr" link set veth0 up
ip -net "$nsbr" link set veth1 up

ip -net "$nsbr" link set veth0 master br0
ip -net "$nsbr" link set veth1 master br0
ip -net "$nsbr" link set br0 up
ip -net "$nsbr" addr add 10.0.0.1/24 dev br0

# place both in same subnet, ${ns1} and ${ns2} connected via ${nsbr}:br0
ip -net "$ns1" link set eth0 up
ip -net "$ns2" link set eth0 up
ip -net "$ns1" addr add 10.0.0.11/24 dev eth0
ip -net "$ns2" addr add 10.0.0.12/24 dev eth0

test_ebtables_broute()
{
	# redirect is needed so the dstmac is rewritten to the bridge itself,
	# ip stack won't process OTHERHOST (foreign unicast mac) packets.
	if ! ip netns exec "$nsbr" ebtables -t broute -A BROUTING -p ipv4 --ip-protocol icmp -j redirect --redirect-target=DROP; then
		echo "SKIP: Could not add ebtables broute redirect rule"
		return $ksft_skip
	fi

	ip netns exec "$nsbr" sysctl -q net.ipv4.conf.veth0.forwarding=0

	# ping net${ns1}, expected to not work (ip forwarding is off)
	if ip netns exec "$ns1" ping -q -c 1 10.0.0.12 -W 0.5 > /dev/null 2>&1; then
		echo "ERROR: ping works, should have failed" 1>&2
		return 1
	fi

	# enable forwarding on both interfaces.
	# neither needs an ip address, but at least the bridge needs
	# an ip address in same network segment as ${ns1} and ${ns2} (${nsbr}
	# needs to be able to determine route for to-be-forwarded packet).
	ip netns exec "$nsbr" sysctl -q net.ipv4.conf.veth0.forwarding=1
	ip netns exec "$nsbr" sysctl -q net.ipv4.conf.veth1.forwarding=1

	if ! ip netns exec "$ns1" ping -q -c 1 10.0.0.12 > /dev/null; then
		echo "ERROR: ping did not work, but it should (broute+forward)" 1>&2
		return 1
	fi

	echo "PASS: ${ns1}/${ns2} connectivity with active broute rule"
	ip netns exec "$nsbr" ebtables -t broute -F

	# ping net${ns1}, expected to work (frames are bridged)
	if ! ip netns exec "$ns1" ping -q -c 1 10.0.0.12 > /dev/null; then
		echo "ERROR: ping did not work, but it should (bridged)" 1>&2
		return 1
	fi

	ip netns exec "$nsbr" ebtables -t filter -A FORWARD -p ipv4 --ip-protocol icmp -j DROP

	# ping net${ns1}, expected to not work (DROP in bridge forward)
	if ip netns exec "$ns1" ping -q -c 1 10.0.0.12 -W 0.5 > /dev/null 2>&1; then
		echo "ERROR: ping works, should have failed (icmp forward drop)" 1>&2
		return 1
	fi

	# re-activate brouter
	ip netns exec "$nsbr" ebtables -t broute -A BROUTING -p ipv4 --ip-protocol icmp -j redirect --redirect-target=DROP

	if ! ip netns exec "$ns2" ping -q -c 1 10.0.0.11 > /dev/null; then
		echo "ERROR: ping did not work, but it should (broute+forward 2)" 1>&2
		return 1
	fi

	echo "PASS: ${ns1}/${ns2} connectivity with active broute rule and bridge forward drop"
	return 0
}

# test basic connectivity
if ! ip netns exec "$ns1" ping -c 1 -q 10.0.0.12 > /dev/null; then
    echo "ERROR: Could not reach ${ns2} from ${ns1}" 1>&2
    exit 1
fi

if ! ip netns exec "$ns2" ping -c 1 -q 10.0.0.11 > /dev/null; then
    echo "ERROR: Could not reach ${ns1} from ${ns2}" 1>&2
    exit 1
fi

test_ebtables_broute
exit $?
