#!/bin/sh
#
# This test is for checking rtnetlink callpaths, and get as much coverage as possible.
#
# set -e

devdummy="test-dummy0"
ret=0

# set global exit status, but never reset nonzero one.
check_err()
{
	if [ $ret -eq 0 ]; then
		ret=$1
	fi
}

kci_add_dummy()
{
	ip link add name "$devdummy" type dummy
	check_err $?
	ip link set "$devdummy" up
	check_err $?
}

kci_del_dummy()
{
	ip link del dev "$devdummy"
	check_err $?
}

# add a bridge with vlans on top
kci_test_bridge()
{
	devbr="test-br0"
	vlandev="testbr-vlan1"

	ret=0
	ip link add name "$devbr" type bridge
	check_err $?

	ip link set dev "$devdummy" master "$devbr"
	check_err $?

	ip link set "$devbr" up
	check_err $?

	ip link add link "$devbr" name "$vlandev" type vlan id 1
	check_err $?
	ip addr add dev "$vlandev" 10.200.7.23/30
	check_err $?
	ip -6 addr add dev "$vlandev" dead:42::1234/64
	check_err $?
	ip -d link > /dev/null
	check_err $?
	ip r s t all > /dev/null
	check_err $?
	ip -6 addr del dev "$vlandev" dead:42::1234/64
	check_err $?

	ip link del dev "$vlandev"
	check_err $?
	ip link del dev "$devbr"
	check_err $?

	if [ $ret -ne 0 ];then
		echo "FAIL: bridge setup"
		return 1
	fi
	echo "PASS: bridge setup"

}

kci_test_gre()
{
	gredev=neta
	rem=10.42.42.1
	loc=10.0.0.1

	ret=0
	ip tunnel add $gredev mode gre remote $rem local $loc ttl 1
	check_err $?
	ip link set $gredev up
	check_err $?
	ip addr add 10.23.7.10 dev $gredev
	check_err $?
	ip route add 10.23.8.0/30 dev $gredev
	check_err $?
	ip addr add dev "$devdummy" 10.23.7.11/24
	check_err $?
	ip link > /dev/null
	check_err $?
	ip addr > /dev/null
	check_err $?
	ip addr del dev "$devdummy" 10.23.7.11/24
	check_err $?

	ip link del $gredev
	check_err $?

	if [ $ret -ne 0 ];then
		echo "FAIL: gre tunnel endpoint"
		return 1
	fi
	echo "PASS: gre tunnel endpoint"
}

# tc uses rtnetlink too, for full tc testing
# please see tools/testing/selftests/tc-testing.
kci_test_tc()
{
	dev=lo
	ret=0

	tc qdisc add dev "$dev" root handle 1: htb
	check_err $?
	tc class add dev "$dev" parent 1: classid 1:10 htb rate 1mbit
	check_err $?
	tc filter add dev "$dev" parent 1:0 prio 5 handle ffe: protocol ip u32 divisor 256
	check_err $?
	tc filter add dev "$dev" parent 1:0 prio 5 handle ffd: protocol ip u32 divisor 256
	check_err $?
	tc filter add dev "$dev" parent 1:0 prio 5 handle ffc: protocol ip u32 divisor 256
	check_err $?
	tc filter add dev "$dev" protocol ip parent 1: prio 5 handle ffe:2:3 u32 ht ffe:2: match ip src 10.0.0.3 flowid 1:10
	check_err $?
	tc filter add dev "$dev" protocol ip parent 1: prio 5 handle ffe:2:2 u32 ht ffe:2: match ip src 10.0.0.2 flowid 1:10
	check_err $?
	tc filter show dev "$dev" parent  1:0 > /dev/null
	check_err $?
	tc filter del dev "$dev" protocol ip parent 1: prio 5 handle ffe:2:3 u32
	check_err $?
	tc filter show dev "$dev" parent  1:0 > /dev/null
	check_err $?
	tc qdisc del dev "$dev" root handle 1: htb
	check_err $?

	if [ $ret -ne 0 ];then
		echo "FAIL: tc htb hierarchy"
		return 1
	fi
	echo "PASS: tc htb hierarchy"

}

kci_test_polrouting()
{
	ret=0
	ip rule add fwmark 1 lookup 100
	check_err $?
	ip route add local 0.0.0.0/0 dev lo table 100
	check_err $?
	ip r s t all > /dev/null
	check_err $?
	ip rule del fwmark 1 lookup 100
	check_err $?
	ip route del local 0.0.0.0/0 dev lo table 100
	check_err $?

	if [ $ret -ne 0 ];then
		echo "FAIL: policy route test"
		return 1
	fi
	echo "PASS: policy routing"
}

kci_test_route_get()
{
	ret=0

	ip route get 127.0.0.1 > /dev/null
	check_err $?
	ip route get 127.0.0.1 dev "$devdummy" > /dev/null
	check_err $?
	ip route get ::1 > /dev/null
	check_err $?
	ip route get fe80::1 dev "$devdummy" > /dev/null
	check_err $?
	ip route get 127.0.0.1 from 127.0.0.1 oif lo tos 0x1 mark 0x1 > /dev/null
	check_err $?
	ip route get ::1 from ::1 iif lo oif lo tos 0x1 mark 0x1 > /dev/null
	check_err $?
	ip addr add dev "$devdummy" 10.23.7.11/24
	check_err $?
	ip route get 10.23.7.11 from 10.23.7.12 iif "$devdummy" > /dev/null
	check_err $?
	ip addr del dev "$devdummy" 10.23.7.11/24
	check_err $?

	if [ $ret -ne 0 ];then
		echo "FAIL: route get"
		return 1
	fi

	echo "PASS: route get"
}

kci_test_addrlabel()
{
	ret=0

	ip addrlabel add prefix dead::/64 dev lo label 1
	check_err $?

	ip addrlabel list |grep -q "prefix dead::/64 dev lo label 1"
	check_err $?

	ip addrlabel del prefix dead::/64 dev lo label 1 2> /dev/null
	check_err $?

	ip addrlabel add prefix dead::/64 label 1 2> /dev/null
	check_err $?

	ip addrlabel del prefix dead::/64 label 1 2> /dev/null
	check_err $?

	# concurrent add/delete
	for i in $(seq 1 1000); do
		ip addrlabel add prefix 1c3::/64 label 12345 2>/dev/null
	done &

	for i in $(seq 1 1000); do
		ip addrlabel del prefix 1c3::/64 label 12345 2>/dev/null
	done

	wait

	ip addrlabel del prefix 1c3::/64 label 12345 2>/dev/null

	if [ $ret -ne 0 ];then
		echo "FAIL: ipv6 addrlabel"
		return 1
	fi

	echo "PASS: ipv6 addrlabel"
}

kci_test_rtnl()
{
	kci_add_dummy
	if [ $ret -ne 0 ];then
		echo "FAIL: cannot add dummy interface"
		return 1
	fi

	kci_test_polrouting
	kci_test_route_get
	kci_test_tc
	kci_test_gre
	kci_test_bridge
	kci_test_addrlabel

	kci_del_dummy
}

#check for needed privileges
if [ "$(id -u)" -ne 0 ];then
	echo "SKIP: Need root privileges"
	exit 0
fi

for x in ip tc;do
	$x -Version 2>/dev/null >/dev/null
	if [ $? -ne 0 ];then
		echo "SKIP: Could not run test without the $x tool"
		exit 0
	fi
done

kci_test_rtnl

exit $ret
