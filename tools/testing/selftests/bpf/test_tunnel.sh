#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# End-to-end eBPF tunnel test suite
#   The script tests BPF network tunnel implementation.
#
# Topology:
# ---------
#     root namespace   |     at_ns0 namespace
#                      |
#      -----------     |     -----------
#      | tnl dev |     |     | tnl dev |  (overlay network)
#      -----------     |     -----------
#      metadata-mode   |     native-mode
#       with bpf       |
#                      |
#      ----------      |     ----------
#      |  veth1  | --------- |  veth0  |  (underlay network)
#      ----------    peer    ----------
#
#
# Device Configuration
# --------------------
# Root namespace with metadata-mode tunnel + BPF
# Device names and addresses:
# 	veth1 IP: 172.16.1.200, IPv6: 00::22 (underlay)
# 	tunnel dev <type>11, ex: gre11, IPv4: 10.1.1.200 (overlay)
#
# Namespace at_ns0 with native tunnel
# Device names and addresses:
# 	veth0 IPv4: 172.16.1.100, IPv6: 00::11 (underlay)
# 	tunnel dev <type>00, ex: gre00, IPv4: 10.1.1.100 (overlay)
#
#
# End-to-end ping packet flow
# ---------------------------
# Most of the tests start by namespace creation, device configuration,
# then ping the underlay and overlay network.  When doing 'ping 10.1.1.100'
# from root namespace, the following operations happen:
# 1) Route lookup shows 10.1.1.100/24 belongs to tnl dev, fwd to tnl dev.
# 2) Tnl device's egress BPF program is triggered and set the tunnel metadata,
#    with remote_ip=172.16.1.200 and others.
# 3) Outer tunnel header is prepended and route the packet to veth1's egress
# 4) veth0's ingress queue receive the tunneled packet at namespace at_ns0
# 5) Tunnel protocol handler, ex: vxlan_rcv, decap the packet
# 6) Forward the packet to the overlay tnl dev

PING_ARG="-c 3 -w 10 -q"
ret=0
GREEN='\033[0;92m'
RED='\033[0;31m'
NC='\033[0m' # No Color

config_device()
{
	ip netns add at_ns0
	ip link add veth0 type veth peer name veth1
	ip link set veth0 netns at_ns0
	ip netns exec at_ns0 ip addr add 172.16.1.100/24 dev veth0
	ip netns exec at_ns0 ip link set dev veth0 up
	ip link set dev veth1 up mtu 1500
	ip addr add dev veth1 172.16.1.200/24
}

add_gre_tunnel()
{
	# at_ns0 namespace
	ip netns exec at_ns0 \
        ip link add dev $DEV_NS type $TYPE seq key 2 \
		local 172.16.1.100 remote 172.16.1.200
	ip netns exec at_ns0 ip link set dev $DEV_NS up
	ip netns exec at_ns0 ip addr add dev $DEV_NS 10.1.1.100/24

	# root namespace
	ip link add dev $DEV type $TYPE key 2 external
	ip link set dev $DEV up
	ip addr add dev $DEV 10.1.1.200/24
}

add_ip6gretap_tunnel()
{

	# assign ipv6 address
	ip netns exec at_ns0 ip addr add ::11/96 dev veth0
	ip netns exec at_ns0 ip link set dev veth0 up
	ip addr add dev veth1 ::22/96
	ip link set dev veth1 up

	# at_ns0 namespace
	ip netns exec at_ns0 \
		ip link add dev $DEV_NS type $TYPE seq flowlabel 0xbcdef key 2 \
		local ::11 remote ::22

	ip netns exec at_ns0 ip addr add dev $DEV_NS 10.1.1.100/24
	ip netns exec at_ns0 ip addr add dev $DEV_NS fc80::100/96
	ip netns exec at_ns0 ip link set dev $DEV_NS up

	# root namespace
	ip link add dev $DEV type $TYPE external
	ip addr add dev $DEV 10.1.1.200/24
	ip addr add dev $DEV fc80::200/24
	ip link set dev $DEV up
}

add_erspan_tunnel()
{
	# at_ns0 namespace
	if [ "$1" == "v1" ]; then
		ip netns exec at_ns0 \
		ip link add dev $DEV_NS type $TYPE seq key 2 \
		local 172.16.1.100 remote 172.16.1.200 \
		erspan_ver 1 erspan 123
	else
		ip netns exec at_ns0 \
		ip link add dev $DEV_NS type $TYPE seq key 2 \
		local 172.16.1.100 remote 172.16.1.200 \
		erspan_ver 2 erspan_dir egress erspan_hwid 3
	fi
	ip netns exec at_ns0 ip link set dev $DEV_NS up
	ip netns exec at_ns0 ip addr add dev $DEV_NS 10.1.1.100/24

	# root namespace
	ip link add dev $DEV type $TYPE external
	ip link set dev $DEV up
	ip addr add dev $DEV 10.1.1.200/24
}

add_ip6erspan_tunnel()
{

	# assign ipv6 address
	ip netns exec at_ns0 ip addr add ::11/96 dev veth0
	ip netns exec at_ns0 ip link set dev veth0 up
	ip addr add dev veth1 ::22/96
	ip link set dev veth1 up

	# at_ns0 namespace
	if [ "$1" == "v1" ]; then
		ip netns exec at_ns0 \
		ip link add dev $DEV_NS type $TYPE seq key 2 \
		local ::11 remote ::22 \
		erspan_ver 1 erspan 123
	else
		ip netns exec at_ns0 \
		ip link add dev $DEV_NS type $TYPE seq key 2 \
		local ::11 remote ::22 \
		erspan_ver 2 erspan_dir egress erspan_hwid 7
	fi
	ip netns exec at_ns0 ip addr add dev $DEV_NS 10.1.1.100/24
	ip netns exec at_ns0 ip link set dev $DEV_NS up

	# root namespace
	ip link add dev $DEV type $TYPE external
	ip addr add dev $DEV 10.1.1.200/24
	ip link set dev $DEV up
}

add_vxlan_tunnel()
{
	# Set static ARP entry here because iptables set-mark works
	# on L3 packet, as a result not applying to ARP packets,
	# causing errors at get_tunnel_{key/opt}.

	# at_ns0 namespace
	ip netns exec at_ns0 \
		ip link add dev $DEV_NS type $TYPE \
		id 2 dstport 4789 gbp remote 172.16.1.200
	ip netns exec at_ns0 \
		ip link set dev $DEV_NS address 52:54:00:d9:01:00 up
	ip netns exec at_ns0 ip addr add dev $DEV_NS 10.1.1.100/24
	ip netns exec at_ns0 arp -s 10.1.1.200 52:54:00:d9:02:00
	ip netns exec at_ns0 iptables -A OUTPUT -j MARK --set-mark 0x800FF

	# root namespace
	ip link add dev $DEV type $TYPE external gbp dstport 4789
	ip link set dev $DEV address 52:54:00:d9:02:00 up
	ip addr add dev $DEV 10.1.1.200/24
	arp -s 10.1.1.100 52:54:00:d9:01:00
}

add_ip6vxlan_tunnel()
{
	#ip netns exec at_ns0 ip -4 addr del 172.16.1.100 dev veth0
	ip netns exec at_ns0 ip -6 addr add ::11/96 dev veth0
	ip netns exec at_ns0 ip link set dev veth0 up
	#ip -4 addr del 172.16.1.200 dev veth1
	ip -6 addr add dev veth1 ::22/96
	ip link set dev veth1 up

	# at_ns0 namespace
	ip netns exec at_ns0 \
		ip link add dev $DEV_NS type $TYPE id 22 dstport 4789 \
		local ::11 remote ::22
	ip netns exec at_ns0 ip addr add dev $DEV_NS 10.1.1.100/24
	ip netns exec at_ns0 ip link set dev $DEV_NS up

	# root namespace
	ip link add dev $DEV type $TYPE external dstport 4789
	ip addr add dev $DEV 10.1.1.200/24
	ip link set dev $DEV up
}

add_geneve_tunnel()
{
	# at_ns0 namespace
	ip netns exec at_ns0 \
		ip link add dev $DEV_NS type $TYPE \
		id 2 dstport 6081 remote 172.16.1.200
	ip netns exec at_ns0 ip link set dev $DEV_NS up
	ip netns exec at_ns0 ip addr add dev $DEV_NS 10.1.1.100/24

	# root namespace
	ip link add dev $DEV type $TYPE dstport 6081 external
	ip link set dev $DEV up
	ip addr add dev $DEV 10.1.1.200/24
}

add_ip6geneve_tunnel()
{
	ip netns exec at_ns0 ip addr add ::11/96 dev veth0
	ip netns exec at_ns0 ip link set dev veth0 up
	ip addr add dev veth1 ::22/96
	ip link set dev veth1 up

	# at_ns0 namespace
	ip netns exec at_ns0 \
		ip link add dev $DEV_NS type $TYPE id 22 \
		remote ::22     # geneve has no local option
	ip netns exec at_ns0 ip addr add dev $DEV_NS 10.1.1.100/24
	ip netns exec at_ns0 ip link set dev $DEV_NS up

	# root namespace
	ip link add dev $DEV type $TYPE external
	ip addr add dev $DEV 10.1.1.200/24
	ip link set dev $DEV up
}

add_ipip_tunnel()
{
	# at_ns0 namespace
	ip netns exec at_ns0 \
		ip link add dev $DEV_NS type $TYPE \
		local 172.16.1.100 remote 172.16.1.200
	ip netns exec at_ns0 ip link set dev $DEV_NS up
	ip netns exec at_ns0 ip addr add dev $DEV_NS 10.1.1.100/24

	# root namespace
	ip link add dev $DEV type $TYPE external
	ip link set dev $DEV up
	ip addr add dev $DEV 10.1.1.200/24
}

add_ipip6tnl_tunnel()
{
	ip netns exec at_ns0 ip addr add ::11/96 dev veth0
	ip netns exec at_ns0 ip link set dev veth0 up
	ip addr add dev veth1 ::22/96
	ip link set dev veth1 up

	# at_ns0 namespace
	ip netns exec at_ns0 \
		ip link add dev $DEV_NS type $TYPE \
		local ::11 remote ::22
	ip netns exec at_ns0 ip addr add dev $DEV_NS 10.1.1.100/24
	ip netns exec at_ns0 ip link set dev $DEV_NS up

	# root namespace
	ip link add dev $DEV type $TYPE external
	ip addr add dev $DEV 10.1.1.200/24
	ip link set dev $DEV up
}

test_gre()
{
	TYPE=gretap
	DEV_NS=gretap00
	DEV=gretap11
	ret=0

	check $TYPE
	config_device
	add_gre_tunnel
	attach_bpf $DEV gre_set_tunnel gre_get_tunnel
	ping $PING_ARG 10.1.1.100
	check_err $?
	ip netns exec at_ns0 ping $PING_ARG 10.1.1.200
	check_err $?
	cleanup

        if [ $ret -ne 0 ]; then
                echo -e ${RED}"FAIL: $TYPE"${NC}
                return 1
        fi
        echo -e ${GREEN}"PASS: $TYPE"${NC}
}

test_ip6gre()
{
	TYPE=ip6gre
	DEV_NS=ip6gre00
	DEV=ip6gre11
	ret=0

	check $TYPE
	config_device
	# reuse the ip6gretap function
	add_ip6gretap_tunnel
	attach_bpf $DEV ip6gretap_set_tunnel ip6gretap_get_tunnel
	# underlay
	ping6 $PING_ARG ::11
	# overlay: ipv4 over ipv6
	ip netns exec at_ns0 ping $PING_ARG 10.1.1.200
	ping $PING_ARG 10.1.1.100
	check_err $?
	# overlay: ipv6 over ipv6
	ip netns exec at_ns0 ping6 $PING_ARG fc80::200
	check_err $?
	cleanup

        if [ $ret -ne 0 ]; then
                echo -e ${RED}"FAIL: $TYPE"${NC}
                return 1
        fi
        echo -e ${GREEN}"PASS: $TYPE"${NC}
}

test_ip6gretap()
{
	TYPE=ip6gretap
	DEV_NS=ip6gretap00
	DEV=ip6gretap11
	ret=0

	check $TYPE
	config_device
	add_ip6gretap_tunnel
	attach_bpf $DEV ip6gretap_set_tunnel ip6gretap_get_tunnel
	# underlay
	ping6 $PING_ARG ::11
	# overlay: ipv4 over ipv6
	ip netns exec at_ns0 ping $PING_ARG 10.1.1.200
	ping $PING_ARG 10.1.1.100
	check_err $?
	# overlay: ipv6 over ipv6
	ip netns exec at_ns0 ping6 $PING_ARG fc80::200
	check_err $?
	cleanup

	if [ $ret -ne 0 ]; then
                echo -e ${RED}"FAIL: $TYPE"${NC}
                return 1
        fi
        echo -e ${GREEN}"PASS: $TYPE"${NC}
}

test_erspan()
{
	TYPE=erspan
	DEV_NS=erspan00
	DEV=erspan11
	ret=0

	check $TYPE
	config_device
	add_erspan_tunnel $1
	attach_bpf $DEV erspan_set_tunnel erspan_get_tunnel
	ping $PING_ARG 10.1.1.100
	check_err $?
	ip netns exec at_ns0 ping $PING_ARG 10.1.1.200
	check_err $?
	cleanup

	if [ $ret -ne 0 ]; then
                echo -e ${RED}"FAIL: $TYPE"${NC}
                return 1
        fi
        echo -e ${GREEN}"PASS: $TYPE"${NC}
}

test_ip6erspan()
{
	TYPE=ip6erspan
	DEV_NS=ip6erspan00
	DEV=ip6erspan11
	ret=0

	check $TYPE
	config_device
	add_ip6erspan_tunnel $1
	attach_bpf $DEV ip4ip6erspan_set_tunnel ip4ip6erspan_get_tunnel
	ping6 $PING_ARG ::11
	ip netns exec at_ns0 ping $PING_ARG 10.1.1.200
	check_err $?
	cleanup

	if [ $ret -ne 0 ]; then
                echo -e ${RED}"FAIL: $TYPE"${NC}
                return 1
        fi
        echo -e ${GREEN}"PASS: $TYPE"${NC}
}

test_vxlan()
{
	TYPE=vxlan
	DEV_NS=vxlan00
	DEV=vxlan11
	ret=0

	check $TYPE
	config_device
	add_vxlan_tunnel
	attach_bpf $DEV vxlan_set_tunnel vxlan_get_tunnel
	ping $PING_ARG 10.1.1.100
	check_err $?
	ip netns exec at_ns0 ping $PING_ARG 10.1.1.200
	check_err $?
	cleanup

	if [ $ret -ne 0 ]; then
                echo -e ${RED}"FAIL: $TYPE"${NC}
                return 1
        fi
        echo -e ${GREEN}"PASS: $TYPE"${NC}
}

test_ip6vxlan()
{
	TYPE=vxlan
	DEV_NS=ip6vxlan00
	DEV=ip6vxlan11
	ret=0

	check $TYPE
	config_device
	add_ip6vxlan_tunnel
	ip link set dev veth1 mtu 1500
	attach_bpf $DEV ip6vxlan_set_tunnel ip6vxlan_get_tunnel
	# underlay
	ping6 $PING_ARG ::11
	# ip4 over ip6
	ping $PING_ARG 10.1.1.100
	check_err $?
	ip netns exec at_ns0 ping $PING_ARG 10.1.1.200
	check_err $?
	cleanup

	if [ $ret -ne 0 ]; then
                echo -e ${RED}"FAIL: ip6$TYPE"${NC}
                return 1
        fi
        echo -e ${GREEN}"PASS: ip6$TYPE"${NC}
}

test_geneve()
{
	TYPE=geneve
	DEV_NS=geneve00
	DEV=geneve11
	ret=0

	check $TYPE
	config_device
	add_geneve_tunnel
	attach_bpf $DEV geneve_set_tunnel geneve_get_tunnel
	ping $PING_ARG 10.1.1.100
	check_err $?
	ip netns exec at_ns0 ping $PING_ARG 10.1.1.200
	check_err $?
	cleanup

	if [ $ret -ne 0 ]; then
                echo -e ${RED}"FAIL: $TYPE"${NC}
                return 1
        fi
        echo -e ${GREEN}"PASS: $TYPE"${NC}
}

test_ip6geneve()
{
	TYPE=geneve
	DEV_NS=ip6geneve00
	DEV=ip6geneve11
	ret=0

	check $TYPE
	config_device
	add_ip6geneve_tunnel
	attach_bpf $DEV ip6geneve_set_tunnel ip6geneve_get_tunnel
	ping $PING_ARG 10.1.1.100
	check_err $?
	ip netns exec at_ns0 ping $PING_ARG 10.1.1.200
	check_err $?
	cleanup

	if [ $ret -ne 0 ]; then
                echo -e ${RED}"FAIL: ip6$TYPE"${NC}
                return 1
        fi
        echo -e ${GREEN}"PASS: ip6$TYPE"${NC}
}

test_ipip()
{
	TYPE=ipip
	DEV_NS=ipip00
	DEV=ipip11
	ret=0

	check $TYPE
	config_device
	add_ipip_tunnel
	ip link set dev veth1 mtu 1500
	attach_bpf $DEV ipip_set_tunnel ipip_get_tunnel
	ping $PING_ARG 10.1.1.100
	check_err $?
	ip netns exec at_ns0 ping $PING_ARG 10.1.1.200
	check_err $?
	cleanup

	if [ $ret -ne 0 ]; then
                echo -e ${RED}"FAIL: $TYPE"${NC}
                return 1
        fi
        echo -e ${GREEN}"PASS: $TYPE"${NC}
}

test_ipip6()
{
	TYPE=ip6tnl
	DEV_NS=ipip6tnl00
	DEV=ipip6tnl11
	ret=0

	check $TYPE
	config_device
	add_ipip6tnl_tunnel
	ip link set dev veth1 mtu 1500
	attach_bpf $DEV ipip6_set_tunnel ipip6_get_tunnel
	# underlay
	ping6 $PING_ARG ::11
	# ip4 over ip6
	ping $PING_ARG 10.1.1.100
	check_err $?
	ip netns exec at_ns0 ping $PING_ARG 10.1.1.200
	check_err $?
	cleanup

	if [ $ret -ne 0 ]; then
                echo -e ${RED}"FAIL: $TYPE"${NC}
                return 1
        fi
        echo -e ${GREEN}"PASS: $TYPE"${NC}
}

setup_xfrm_tunnel()
{
	auth=0x$(printf '1%.0s' {1..40})
	enc=0x$(printf '2%.0s' {1..32})
	spi_in_to_out=0x1
	spi_out_to_in=0x2
	# at_ns0 namespace
	# at_ns0 -> root
	ip netns exec at_ns0 \
		ip xfrm state add src 172.16.1.100 dst 172.16.1.200 proto esp \
			spi $spi_in_to_out reqid 1 mode tunnel \
			auth-trunc 'hmac(sha1)' $auth 96 enc 'cbc(aes)' $enc
	ip netns exec at_ns0 \
		ip xfrm policy add src 10.1.1.100/32 dst 10.1.1.200/32 dir out \
		tmpl src 172.16.1.100 dst 172.16.1.200 proto esp reqid 1 \
		mode tunnel
	# root -> at_ns0
	ip netns exec at_ns0 \
		ip xfrm state add src 172.16.1.200 dst 172.16.1.100 proto esp \
			spi $spi_out_to_in reqid 2 mode tunnel \
			auth-trunc 'hmac(sha1)' $auth 96 enc 'cbc(aes)' $enc
	ip netns exec at_ns0 \
		ip xfrm policy add src 10.1.1.200/32 dst 10.1.1.100/32 dir in \
		tmpl src 172.16.1.200 dst 172.16.1.100 proto esp reqid 2 \
		mode tunnel
	# address & route
	ip netns exec at_ns0 \
		ip addr add dev veth0 10.1.1.100/32
	ip netns exec at_ns0 \
		ip route add 10.1.1.200 dev veth0 via 172.16.1.200 \
			src 10.1.1.100

	# root namespace
	# at_ns0 -> root
	ip xfrm state add src 172.16.1.100 dst 172.16.1.200 proto esp \
		spi $spi_in_to_out reqid 1 mode tunnel \
		auth-trunc 'hmac(sha1)' $auth 96  enc 'cbc(aes)' $enc
	ip xfrm policy add src 10.1.1.100/32 dst 10.1.1.200/32 dir in \
		tmpl src 172.16.1.100 dst 172.16.1.200 proto esp reqid 1 \
		mode tunnel
	# root -> at_ns0
	ip xfrm state add src 172.16.1.200 dst 172.16.1.100 proto esp \
		spi $spi_out_to_in reqid 2 mode tunnel \
		auth-trunc 'hmac(sha1)' $auth 96  enc 'cbc(aes)' $enc
	ip xfrm policy add src 10.1.1.200/32 dst 10.1.1.100/32 dir out \
		tmpl src 172.16.1.200 dst 172.16.1.100 proto esp reqid 2 \
		mode tunnel
	# address & route
	ip addr add dev veth1 10.1.1.200/32
	ip route add 10.1.1.100 dev veth1 via 172.16.1.100 src 10.1.1.200
}

test_xfrm_tunnel()
{
	config_device
        #tcpdump -nei veth1 ip &
	output=$(mktemp)
	cat /sys/kernel/debug/tracing/trace_pipe | tee $output &
        setup_xfrm_tunnel
	tc qdisc add dev veth1 clsact
	tc filter add dev veth1 proto ip ingress bpf da obj test_tunnel_kern.o \
		sec xfrm_get_state
	ip netns exec at_ns0 ping $PING_ARG 10.1.1.200
	sleep 1
	grep "reqid 1" $output
	check_err $?
	grep "spi 0x1" $output
	check_err $?
	grep "remote ip 0xac100164" $output
	check_err $?
	cleanup

	if [ $ret -ne 0 ]; then
                echo -e ${RED}"FAIL: xfrm tunnel"${NC}
                return 1
        fi
        echo -e ${GREEN}"PASS: xfrm tunnel"${NC}
}

attach_bpf()
{
	DEV=$1
	SET=$2
	GET=$3
	tc qdisc add dev $DEV clsact
	tc filter add dev $DEV egress bpf da obj test_tunnel_kern.o sec $SET
	tc filter add dev $DEV ingress bpf da obj test_tunnel_kern.o sec $GET
}

cleanup()
{
	ip netns delete at_ns0 2> /dev/null
	ip link del veth1 2> /dev/null
	ip link del ipip11 2> /dev/null
	ip link del ipip6tnl11 2> /dev/null
	ip link del gretap11 2> /dev/null
	ip link del ip6gre11 2> /dev/null
	ip link del ip6gretap11 2> /dev/null
	ip link del vxlan11 2> /dev/null
	ip link del ip6vxlan11 2> /dev/null
	ip link del geneve11 2> /dev/null
	ip link del ip6geneve11 2> /dev/null
	ip link del erspan11 2> /dev/null
	ip link del ip6erspan11 2> /dev/null
}

cleanup_exit()
{
	echo "CATCH SIGKILL or SIGINT, cleanup and exit"
	cleanup
	exit 0
}

check()
{
	ip link help $1 2>&1 | grep -q "^Usage:"
	if [ $? -ne 0 ];then
		echo "SKIP $1: iproute2 not support"
	cleanup
	return 1
	fi
}

enable_debug()
{
	echo 'file ip_gre.c +p' > /sys/kernel/debug/dynamic_debug/control
	echo 'file ip6_gre.c +p' > /sys/kernel/debug/dynamic_debug/control
	echo 'file vxlan.c +p' > /sys/kernel/debug/dynamic_debug/control
	echo 'file geneve.c +p' > /sys/kernel/debug/dynamic_debug/control
	echo 'file ipip.c +p' > /sys/kernel/debug/dynamic_debug/control
}

check_err()
{
	if [ $ret -eq 0 ]; then
		ret=$1
	fi
}

bpf_tunnel_test()
{
	echo "Testing GRE tunnel..."
	test_gre
	echo "Testing IP6GRE tunnel..."
	test_ip6gre
	echo "Testing IP6GRETAP tunnel..."
	test_ip6gretap
	echo "Testing ERSPAN tunnel..."
	test_erspan v2
	echo "Testing IP6ERSPAN tunnel..."
	test_ip6erspan v2
	echo "Testing VXLAN tunnel..."
	test_vxlan
	echo "Testing IP6VXLAN tunnel..."
	test_ip6vxlan
	echo "Testing GENEVE tunnel..."
	test_geneve
	echo "Testing IP6GENEVE tunnel..."
	test_ip6geneve
	echo "Testing IPIP tunnel..."
	test_ipip
	echo "Testing IPIP6 tunnel..."
	test_ipip6
	echo "Testing IPSec tunnel..."
	test_xfrm_tunnel
}

trap cleanup 0 3 6
trap cleanup_exit 2 9

cleanup
bpf_tunnel_test

exit 0
