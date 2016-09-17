#!/bin/bash

function config_device {
	ip netns add at_ns0
	ip netns add at_ns1
	ip netns add at_ns2
	ip link add veth0 type veth peer name veth0b
	ip link add veth1 type veth peer name veth1b
	ip link add veth2 type veth peer name veth2b
	ip link set veth0b up
	ip link set veth1b up
	ip link set veth2b up
	ip link set dev veth0b mtu 1500
	ip link set dev veth1b mtu 1500
	ip link set dev veth2b mtu 1500
	ip link set veth0 netns at_ns0
	ip link set veth1 netns at_ns1
	ip link set veth2 netns at_ns2
	ip netns exec at_ns0 ip addr add 172.16.1.100/24 dev veth0
	ip netns exec at_ns0 ip addr add 2401:db00::1/64 dev veth0 nodad
	ip netns exec at_ns0 ip link set dev veth0 up
	ip netns exec at_ns1 ip addr add 172.16.1.101/24 dev veth1
	ip netns exec at_ns1 ip addr add 2401:db00::2/64 dev veth1 nodad
	ip netns exec at_ns1 ip link set dev veth1 up
	ip netns exec at_ns2 ip addr add 172.16.1.200/24 dev veth2
	ip netns exec at_ns2 ip addr add 2401:db00::3/64 dev veth2 nodad
	ip netns exec at_ns2 ip link set dev veth2 up
	ip link add br0 type bridge
	ip link set br0 up
	ip link set dev br0 mtu 1500
	ip link set veth0b master br0
	ip link set veth1b master br0
	ip link set veth2b master br0
}

function add_ipip_tunnel {
	ip netns exec at_ns0 \
		ip link add dev $DEV_NS type ipip local 172.16.1.100 remote 172.16.1.200
	ip netns exec at_ns0 ip link set dev $DEV_NS up
	ip netns exec at_ns0 ip addr add dev $DEV_NS 10.1.1.100/24
	ip netns exec at_ns1 \
		ip link add dev $DEV_NS type ipip local 172.16.1.101 remote 172.16.1.200
	ip netns exec at_ns1 ip link set dev $DEV_NS up
	# same inner IP address in at_ns0 and at_ns1
	ip netns exec at_ns1 ip addr add dev $DEV_NS 10.1.1.100/24

	ip netns exec at_ns2 ip link add dev $DEV type ipip external
	ip netns exec at_ns2 ip link set dev $DEV up
	ip netns exec at_ns2 ip addr add dev $DEV 10.1.1.200/24
}

function add_ipip6_tunnel {
	ip netns exec at_ns0 \
		ip link add dev $DEV_NS type ip6tnl mode ipip6 local 2401:db00::1/64 remote 2401:db00::3/64
	ip netns exec at_ns0 ip link set dev $DEV_NS up
	ip netns exec at_ns0 ip addr add dev $DEV_NS 10.1.1.100/24
	ip netns exec at_ns1 \
		ip link add dev $DEV_NS type ip6tnl mode ipip6 local 2401:db00::2/64 remote 2401:db00::3/64
	ip netns exec at_ns1 ip link set dev $DEV_NS up
	# same inner IP address in at_ns0 and at_ns1
	ip netns exec at_ns1 ip addr add dev $DEV_NS 10.1.1.100/24

	ip netns exec at_ns2 ip link add dev $DEV type ip6tnl mode ipip6 external
	ip netns exec at_ns2 ip link set dev $DEV up
	ip netns exec at_ns2 ip addr add dev $DEV 10.1.1.200/24
}

function add_ip6ip6_tunnel {
	ip netns exec at_ns0 \
		ip link add dev $DEV_NS type ip6tnl mode ip6ip6 local 2401:db00::1/64 remote 2401:db00::3/64
	ip netns exec at_ns0 ip link set dev $DEV_NS up
	ip netns exec at_ns0 ip addr add dev $DEV_NS 2601:646::1/64
	ip netns exec at_ns1 \
		ip link add dev $DEV_NS type ip6tnl mode ip6ip6 local 2401:db00::2/64 remote 2401:db00::3/64
	ip netns exec at_ns1 ip link set dev $DEV_NS up
	# same inner IP address in at_ns0 and at_ns1
	ip netns exec at_ns1 ip addr add dev $DEV_NS 2601:646::1/64

	ip netns exec at_ns2 ip link add dev $DEV type ip6tnl mode ip6ip6 external
	ip netns exec at_ns2 ip link set dev $DEV up
	ip netns exec at_ns2 ip addr add dev $DEV 2601:646::2/64
}

function attach_bpf {
	DEV=$1
	SET_TUNNEL=$2
	GET_TUNNEL=$3
	ip netns exec at_ns2 tc qdisc add dev $DEV clsact
	ip netns exec at_ns2 tc filter add dev $DEV egress bpf da obj tcbpf2_kern.o sec $SET_TUNNEL
	ip netns exec at_ns2 tc filter add dev $DEV ingress bpf da obj tcbpf2_kern.o sec $GET_TUNNEL
}

function test_ipip {
	DEV_NS=ipip_std
	DEV=ipip_bpf
	config_device
#	tcpdump -nei br0 &
	cat /sys/kernel/debug/tracing/trace_pipe &

	add_ipip_tunnel
	attach_bpf $DEV ipip_set_tunnel ipip_get_tunnel

	ip netns exec at_ns0 ping -c 1 10.1.1.200
	ip netns exec at_ns2 ping -c 1 10.1.1.100
	ip netns exec at_ns0 iperf -sD -p 5200 > /dev/null
	ip netns exec at_ns1 iperf -sD -p 5201 > /dev/null
	sleep 0.2
	# tcp check _same_ IP over different tunnels
	ip netns exec at_ns2 iperf -c 10.1.1.100 -n 5k -p 5200
	ip netns exec at_ns2 iperf -c 10.1.1.100 -n 5k -p 5201
	cleanup
}

# IPv4 over IPv6 tunnel
function test_ipip6 {
	DEV_NS=ipip_std
	DEV=ipip_bpf
	config_device
#	tcpdump -nei br0 &
	cat /sys/kernel/debug/tracing/trace_pipe &

	add_ipip6_tunnel
	attach_bpf $DEV ipip6_set_tunnel ipip6_get_tunnel

	ip netns exec at_ns0 ping -c 1 10.1.1.200
	ip netns exec at_ns2 ping -c 1 10.1.1.100
	ip netns exec at_ns0 iperf -sD -p 5200 > /dev/null
	ip netns exec at_ns1 iperf -sD -p 5201 > /dev/null
	sleep 0.2
	# tcp check _same_ IP over different tunnels
	ip netns exec at_ns2 iperf -c 10.1.1.100 -n 5k -p 5200
	ip netns exec at_ns2 iperf -c 10.1.1.100 -n 5k -p 5201
	cleanup
}

# IPv6 over IPv6 tunnel
function test_ip6ip6 {
	DEV_NS=ipip_std
	DEV=ipip_bpf
	config_device
#	tcpdump -nei br0 &
	cat /sys/kernel/debug/tracing/trace_pipe &

	add_ip6ip6_tunnel
	attach_bpf $DEV ip6ip6_set_tunnel ip6ip6_get_tunnel

	ip netns exec at_ns0 ping -6 -c 1 2601:646::2
	ip netns exec at_ns2 ping -6 -c 1 2601:646::1
	ip netns exec at_ns0 iperf -6sD -p 5200 > /dev/null
	ip netns exec at_ns1 iperf -6sD -p 5201 > /dev/null
	sleep 0.2
	# tcp check _same_ IP over different tunnels
	ip netns exec at_ns2 iperf -6c 2601:646::1 -n 5k -p 5200
	ip netns exec at_ns2 iperf -6c 2601:646::1 -n 5k -p 5201
	cleanup
}

function cleanup {
	set +ex
	pkill iperf
	ip netns delete at_ns0
	ip netns delete at_ns1
	ip netns delete at_ns2
	ip link del veth0
	ip link del veth1
	ip link del veth2
	ip link del br0
	pkill tcpdump
	pkill cat
	set -ex
}

cleanup
echo "Testing IP tunnels..."
test_ipip
test_ipip6
test_ip6ip6
echo "*** PASS ***"
