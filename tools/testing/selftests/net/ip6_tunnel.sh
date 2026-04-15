#!/bin/bash
# Test that IPv4-over-IPv6 tunneling works.

source lib.sh
set -e

setup_prepare() {
  ip link add transport1 type veth peer name transport2

  setup_ns ns1
  ip link set transport1 netns $ns1
  ip -n $ns1 address add 2001:db8::1/64 dev transport1 nodad
  ip -n $ns1 address add 2001:db8::3/64 dev transport1 nodad
  ip -n $ns1 link set transport1 up
  ip -n $ns1 link add link transport1 name tunnel4 type ip6tnl mode ipip6 local 2001:db8::1 remote 2001:db8::2
  ip -n $ns1 address add 172.0.0.1/32 peer 172.0.0.2/32 dev tunnel4
  ip -n $ns1 link set tunnel4 up
  ip -n $ns1 link add link transport1 name tunnel6 type ip6tnl mode ip6ip6 local 2001:db8::3 remote 2001:db8::4
  ip -n $ns1 address add 2001:db8:6::1/64 dev tunnel6
  ip -n $ns1 link set tunnel6 up

  setup_ns ns2
  ip link set transport2 netns $ns2
  ip -n $ns2 address add 2001:db8::2/64 dev transport2 nodad
  ip -n $ns2 address add 2001:db8::4/64 dev transport2 nodad
  ip -n $ns2 link set transport2 up
  ip -n $ns2 link add link transport2 name tunnel4 type ip6tnl mode ipip6 local 2001:db8::2 remote 2001:db8::1
  ip -n $ns2 address add 172.0.0.2/32 peer 172.0.0.1/32 dev tunnel4
  ip -n $ns2 link set tunnel4 up
  ip -n $ns2 link add link transport2 name tunnel6 type ip6tnl mode ip6ip6 local 2001:db8::4 remote 2001:db8::3
  ip -n $ns2 address add 2001:db8:6::2/64 dev tunnel6
  ip -n $ns2 link set tunnel6 up
}

cleanup() {
  cleanup_all_ns
  # in case the namespaces haven't been set up yet
  ip link delete transport1 &>/dev/null || true
}

trap cleanup EXIT
setup_prepare
ip netns exec $ns1 ping -q -W1 -c1 172.0.0.2 >/dev/null
ip netns exec $ns1 ping -q -W1 -c1 2001:db8:6::2 >/dev/null
