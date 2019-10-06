#!/bin/bash
#
# This test is for bridge 'brouting', i.e. make some packets being routed
# rather than getting bridged even though they arrive on interface that is
# part of a bridge.

#           eth0    br0     eth0
# setup is: ns1 <-> ns0 <-> ns2

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4
ret=0

ebtables -V > /dev/null 2>&1
if [ $? -ne 0 ];then
	echo "SKIP: Could not run test without ebtables"
	exit $ksft_skip
fi

ip -Version > /dev/null 2>&1
if [ $? -ne 0 ];then
	echo "SKIP: Could not run test without ip tool"
	exit $ksft_skip
fi

ip netns add ns0
ip netns add ns1
ip netns add ns2

ip link add veth0 netns ns0 type veth peer name eth0 netns ns1
if [ $? -ne 0 ]; then
	echo "SKIP: Can't create veth device"
	exit $ksft_skip
fi
ip link add veth1 netns ns0 type veth peer name eth0 netns ns2

ip -net ns0 link set lo up
ip -net ns0 link set veth0 up
ip -net ns0 link set veth1 up

ip -net ns0 link add br0 type bridge
if [ $? -ne 0 ]; then
	echo "SKIP: Can't create bridge br0"
	exit $ksft_skip
fi

ip -net ns0 link set veth0 master br0
ip -net ns0 link set veth1 master br0
ip -net ns0 link set br0 up
ip -net ns0 addr add 10.0.0.1/24 dev br0

# place both in same subnet, ns1 and ns2 connected via ns0:br0
for i in 1 2; do
  ip -net ns$i link set lo up
  ip -net ns$i link set eth0 up
  ip -net ns$i addr add 10.0.0.1$i/24 dev eth0
done

test_ebtables_broute()
{
	local cipt

	# redirect is needed so the dstmac is rewritten to the bridge itself,
	# ip stack won't process OTHERHOST (foreign unicast mac) packets.
	ip netns exec ns0 ebtables -t broute -A BROUTING -p ipv4 --ip-protocol icmp -j redirect --redirect-target=DROP
	if [ $? -ne 0 ]; then
		echo "SKIP: Could not add ebtables broute redirect rule"
		return $ksft_skip
	fi

	# ping netns1, expected to not work (ip forwarding is off)
	ip netns exec ns1 ping -q -c 1 10.0.0.12 > /dev/null 2>&1
	if [ $? -eq 0 ]; then
		echo "ERROR: ping works, should have failed" 1>&2
		return 1
	fi

	# enable forwarding on both interfaces.
	# neither needs an ip address, but at least the bridge needs
	# an ip address in same network segment as ns1 and ns2 (ns0
	# needs to be able to determine route for to-be-forwarded packet).
	ip netns exec ns0 sysctl -q net.ipv4.conf.veth0.forwarding=1
	ip netns exec ns0 sysctl -q net.ipv4.conf.veth1.forwarding=1

	sleep 1

	ip netns exec ns1 ping -q -c 1 10.0.0.12 > /dev/null
	if [ $? -ne 0 ]; then
		echo "ERROR: ping did not work, but it should (broute+forward)" 1>&2
		return 1
	fi

	echo "PASS: ns1/ns2 connectivity with active broute rule"
	ip netns exec ns0 ebtables -t broute -F

	# ping netns1, expected to work (frames are bridged)
	ip netns exec ns1 ping -q -c 1 10.0.0.12 > /dev/null
	if [ $? -ne 0 ]; then
		echo "ERROR: ping did not work, but it should (bridged)" 1>&2
		return 1
	fi

	ip netns exec ns0 ebtables -t filter -A FORWARD -p ipv4 --ip-protocol icmp -j DROP

	# ping netns1, expected to not work (DROP in bridge forward)
	ip netns exec ns1 ping -q -c 1 10.0.0.12 > /dev/null 2>&1
	if [ $? -eq 0 ]; then
		echo "ERROR: ping works, should have failed (icmp forward drop)" 1>&2
		return 1
	fi

	# re-activate brouter
	ip netns exec ns0 ebtables -t broute -A BROUTING -p ipv4 --ip-protocol icmp -j redirect --redirect-target=DROP

	ip netns exec ns2 ping -q -c 1 10.0.0.11 > /dev/null
	if [ $? -ne 0 ]; then
		echo "ERROR: ping did not work, but it should (broute+forward 2)" 1>&2
		return 1
	fi

	echo "PASS: ns1/ns2 connectivity with active broute rule and bridge forward drop"
	return 0
}

# test basic connectivity
ip netns exec ns1 ping -c 1 -q 10.0.0.12 > /dev/null
if [ $? -ne 0 ]; then
    echo "ERROR: Could not reach ns2 from ns1" 1>&2
    ret=1
fi

ip netns exec ns2 ping -c 1 -q 10.0.0.11 > /dev/null
if [ $? -ne 0 ]; then
    echo "ERROR: Could not reach ns1 from ns2" 1>&2
    ret=1
fi

if [ $ret -eq 0 ];then
    echo "PASS: netns connectivity: ns1 and ns2 can reach each other"
fi

test_ebtables_broute
ret=$?
for i in 0 1 2; do ip netns del ns$i;done

exit $ret
