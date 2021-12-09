#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

# Author: Taehee Yoo <ap420073@gmail.com>
#
# This script evaluates the AMT driver.
# There are four network-namespaces, LISTENER, SOURCE, GATEWAY, RELAY.
# The role of LISTENER is to listen multicast traffic.
# In order to do that, it send IGMP group join message.
# The role of SOURCE is to send multicast traffic to listener.
# The role of GATEWAY is to work Gateway role of AMT interface.
# The role of RELAY is to work Relay role of AMT interface.
#
#
#       +------------------------+
#       |    LISTENER netns      |
#       |                        |
#       |  +------------------+  |
#       |  |       l_gw       |  |
#       |  |  192.168.0.2/24  |  |
#       |  |  2001:db8::2/64  |  |
#       |  +------------------+  |
#       |            .           |
#       +------------------------+
#                    .
#                    .
#       +-----------------------------------------------------+
#       |            .         GATEWAY netns                  |
#       |            .                                        |
#       |+---------------------------------------------------+|
#       ||           .          br0                          ||
#       || +------------------+       +------------------+   ||
#       || |       gw_l       |       |       amtg       |   ||
#       || |  192.168.0.1/24  |       +--------+---------+   ||
#       || |  2001:db8::1/64  |                |             ||
#       || +------------------+                |             ||
#       |+-------------------------------------|-------------+|
#       |                                      |              |
#       |                             +--------+---------+    |
#       |                             |     gw_relay     |    |
#       |                             |    10.0.0.1/24   |    |
#       |                             +------------------+    |
#       |                                      .              |
#       +-----------------------------------------------------+
#                                              .
#                                              .
#       +-----------------------------------------------------+
#       |                       RELAY netns    .              |
#       |                             +------------------+    |
#       |                             |     relay_gw     |    |
#       |                             |    10.0.0.2/24   |    |
#       |                             +--------+---------+    |
#       |                                      |              |
#       |                                      |              |
#       |  +------------------+       +--------+---------+    |
#       |  |     relay_src    |       |       amtr       |    |
#       |  |   172.17.0.1/24  |       +------------------+    |
#       |  | 2001:db8:3::1/64 |                               |
#       |  +------------------+                               |
#       |            .                                        |
#       |            .                                        |
#       +-----------------------------------------------------+
#                    .
#                    .
#       +------------------------+
#       |            .           |
#       |  +------------------+  |
#       |  |     src_relay    |  |
#       |  |   172.17.0.2/24  |  |
#       |  | 2001:db8:3::2/64 |  |
#       |  +------------------+  |
#       |      SOURCE netns      |
#       +------------------------+
#==============================================================================

readonly LISTENER=$(mktemp -u listener-XXXXXXXX)
readonly GATEWAY=$(mktemp -u gateway-XXXXXXXX)
readonly RELAY=$(mktemp -u relay-XXXXXXXX)
readonly SOURCE=$(mktemp -u source-XXXXXXXX)
ERR=4
err=0

exit_cleanup()
{
	for ns in "$@"; do
		ip netns delete "${ns}" 2>/dev/null || true
	done

	exit $ERR
}

create_namespaces()
{
	ip netns add "${LISTENER}" || exit_cleanup
	ip netns add "${GATEWAY}" || exit_cleanup "${LISTENER}"
	ip netns add "${RELAY}" || exit_cleanup "${LISTENER}" "${GATEWAY}"
	ip netns add "${SOURCE}" || exit_cleanup "${LISTENER}" "${GATEWAY}" \
		"${RELAY}"
}

# The trap function handler
#
exit_cleanup_all()
{
	exit_cleanup "${LISTENER}" "${GATEWAY}" "${RELAY}" "${SOURCE}"
}

setup_interface()
{
	for ns in "${LISTENER}" "${GATEWAY}" "${RELAY}" "${SOURCE}"; do
		ip -netns "${ns}" link set dev lo up
	done;

	ip link add l_gw type veth peer name gw_l
	ip link add gw_relay type veth peer name relay_gw
	ip link add relay_src type veth peer name src_relay

	ip link set l_gw netns "${LISTENER}" up
	ip link set gw_l netns "${GATEWAY}" up
	ip link set gw_relay netns "${GATEWAY}" up
	ip link set relay_gw netns "${RELAY}" up
	ip link set relay_src netns "${RELAY}" up
	ip link set src_relay netns "${SOURCE}" up mtu 1400

	ip netns exec "${LISTENER}" ip a a 192.168.0.2/24 dev l_gw
	ip netns exec "${LISTENER}" ip r a default via 192.168.0.1 dev l_gw
	ip netns exec "${LISTENER}" ip a a 2001:db8::2/64 dev l_gw
	ip netns exec "${LISTENER}" ip r a default via 2001:db8::1 dev l_gw
	ip netns exec "${LISTENER}" ip a a 239.0.0.1/32 dev l_gw autojoin
	ip netns exec "${LISTENER}" ip a a ff0e::5:6/128 dev l_gw autojoin

	ip netns exec "${GATEWAY}" ip a a 192.168.0.1/24 dev gw_l
	ip netns exec "${GATEWAY}" ip a a 2001:db8::1/64 dev gw_l
	ip netns exec "${GATEWAY}" ip a a 10.0.0.1/24 dev gw_relay
	ip netns exec "${GATEWAY}" ip link add br0 type bridge
	ip netns exec "${GATEWAY}" ip link set br0 up
	ip netns exec "${GATEWAY}" ip link set gw_l master br0
	ip netns exec "${GATEWAY}" ip link set gw_l up
	ip netns exec "${GATEWAY}" ip link add amtg master br0 type amt \
		mode gateway local 10.0.0.1 discovery 10.0.0.2 dev gw_relay \
		gateway_port 2268 relay_port 2268
	ip netns exec "${RELAY}" ip a a 10.0.0.2/24 dev relay_gw
	ip netns exec "${RELAY}" ip link add amtr type amt mode relay \
		local 10.0.0.2 dev relay_gw relay_port 2268 max_tunnels 4
	ip netns exec "${RELAY}" ip a a 172.17.0.1/24 dev relay_src
	ip netns exec "${RELAY}" ip a a 2001:db8:3::1/64 dev relay_src
	ip netns exec "${SOURCE}" ip a a 172.17.0.2/24 dev src_relay
	ip netns exec "${SOURCE}" ip a a 2001:db8:3::2/64 dev src_relay
	ip netns exec "${SOURCE}" ip r a default via 172.17.0.1 dev src_relay
	ip netns exec "${SOURCE}" ip r a default via 2001:db8:3::1 dev src_relay
	ip netns exec "${RELAY}" ip link set amtr up
	ip netns exec "${GATEWAY}" ip link set amtg up
}

setup_sysctl()
{
	ip netns exec "${RELAY}" sysctl net.ipv4.ip_forward=1 -w -q
}

setup_iptables()
{
	ip netns exec "${RELAY}" iptables -t mangle -I PREROUTING \
		-d 239.0.0.1 -j TTL --ttl-set 2
	ip netns exec "${RELAY}" ip6tables -t mangle -I PREROUTING \
		-j HL --hl-set 2
}

setup_mcast_routing()
{
	ip netns exec "${RELAY}" smcrouted
	ip netns exec "${RELAY}" smcroutectl a relay_src \
		172.17.0.2 239.0.0.1 amtr
	ip netns exec "${RELAY}" smcroutectl a relay_src \
		2001:db8:3::2 ff0e::5:6 amtr
}

test_remote_ip()
{
	REMOTE=$(ip netns exec "${GATEWAY}" \
		ip -d -j link show amtg | jq .[0].linkinfo.info_data.remote)
	if [ $REMOTE == "\"10.0.0.2\"" ]; then
		printf "TEST: %-60s  [ OK ]\n" "amt discovery"
	else
		printf "TEST: %-60s  [FAIL]\n" "amt discovery"
		ERR=1
	fi
}

send_mcast_torture4()
{
	ip netns exec "${SOURCE}" bash -c \
		'cat /dev/urandom | head -c 1G | nc -w 1 -u 239.0.0.1 4001'
}


send_mcast_torture6()
{
	ip netns exec "${SOURCE}" bash -c \
		'cat /dev/urandom | head -c 1G | nc -w 1 -u ff0e::5:6 6001'
}

check_features()
{
        ip link help 2>&1 | grep -q amt
        if [ $? -ne 0 ]; then
                echo "Missing amt support in iproute2" >&2
                exit_cleanup
        fi
}

test_ipv4_forward()
{
	RESULT4=$(ip netns exec "${LISTENER}" nc -w 1 -l -u 239.0.0.1 4000)
	if [ "$RESULT4" == "172.17.0.2" ]; then
		printf "TEST: %-60s  [ OK ]\n" "IPv4 amt multicast forwarding"
		exit 0
	else
		printf "TEST: %-60s  [FAIL]\n" "IPv4 amt multicast forwarding"
		exit 1
	fi
}

test_ipv6_forward()
{
	RESULT6=$(ip netns exec "${LISTENER}" nc -w 1 -l -u ff0e::5:6 6000)
	if [ "$RESULT6" == "2001:db8:3::2" ]; then
		printf "TEST: %-60s  [ OK ]\n" "IPv6 amt multicast forwarding"
		exit 0
	else
		printf "TEST: %-60s  [FAIL]\n" "IPv6 amt multicast forwarding"
		exit 1
	fi
}

send_mcast4()
{
	sleep 2
	ip netns exec "${SOURCE}" bash -c \
		'echo 172.17.0.2 | nc -w 1 -u 239.0.0.1 4000' &
}

send_mcast6()
{
	sleep 2
	ip netns exec "${SOURCE}" bash -c \
		'echo 2001:db8:3::2 | nc -w 1 -u ff0e::5:6 6000' &
}

check_features

create_namespaces

set -e
trap exit_cleanup_all EXIT

setup_interface
setup_sysctl
setup_iptables
setup_mcast_routing
test_remote_ip
test_ipv4_forward &
pid=$!
send_mcast4
wait $pid || err=$?
if [ $err -eq 1 ]; then
	ERR=1
fi
test_ipv6_forward &
pid=$!
send_mcast6
wait $pid || err=$?
if [ $err -eq 1 ]; then
	ERR=1
fi
send_mcast_torture4
printf "TEST: %-60s  [ OK ]\n" "IPv4 amt traffic forwarding torture"
send_mcast_torture6
printf "TEST: %-60s  [ OK ]\n" "IPv6 amt traffic forwarding torture"
sleep 5
if [ "${ERR}" -eq 1 ]; then
        echo "Some tests failed." >&2
else
        ERR=0
fi
