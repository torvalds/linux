#!/usr/bin/env bash

if [ -n "$LKL_HOST_CONFIG_BSD" ]; then
TEST_TAP_IFNAME=tap
else
TEST_TAP_IFNAME=lkl_test_tap
fi
TEST_IP_NETWORK=192.168.113.0
TEST_IP_NETMASK=24
TEST_IP6_NETWORK=fc03::0
TEST_IP6_NETMASK=64
TEST_MAC0="aa:bb:cc:dd:ee:ff"
TEST_MAC1="aa:bb:cc:dd:ee:aa"
TEST_NETSERVER_PORT=11223

# $1 - count
# $2 - netcount
ip_add()
{
    IP_HEX=$(printf '%.2X%.2X%.2X%.2X\n' \
         `echo $TEST_IP_NETWORK | sed -e 's/\./ /g'`)
    NET_COUNT=$(( 1 << (32 - $TEST_IP_NETMASK) ))
    NEXT_IP_HEX=$(printf %.8X `echo $((0x$IP_HEX + $1 + ${2:-0} * $NET_COUNT))`)
    NEXT_IP=$(printf '%d.%d.%d.%d\n' \
          `echo $NEXT_IP_HEX | sed -r 's/(..)/0x\1 /g'`)
    echo -n "$NEXT_IP"
}

# $1 - count
# $2 - netcount
ip6_add()
{
    IP6_PREFIX=${TEST_IP6_NETWORK%*::*}
    IP6_HOST=${TEST_IP6_NETWORK#*::*}
    echo -n "$(printf "%x" $((0x$IP6_PREFIX+${2:-0})))::$(($IP6_HOST+$1))"
}

ip_host()
{

    ip_add 1 $1
}

ip_lkl()
{
    ip_add 2 $1
}

ip_host_mask()
{
    echo -n "$(ip_host $1)/$TEST_IP_NETMASK"
}

ip_net_mask()
{
    echo "$(ip_add 0 $1)/$TEST_IP_NETMASK"
}

ip6_host()
{
    ip6_add 1 $1
}

ip6_lkl()
{
    ip6_add 2 $1
}

ip6_host_mask()
{
    echo -n "$(ip6_host $1)/$TEST_IP6_NETMASK"
}

ip6_net_mask()
{
    echo "$(ip6_add 0 $1)/$TEST_IP6_NETMASK"
}

tap_ifname()
{
    echo -n "$TEST_TAP_IFNAME${1:-0}"
}

tap_prepare()
{
    if [ -n "$LKL_HOST_CONFIG_ANDROID" ]; then
        if ! lkl_test_cmd test -d /dev/net &>/dev/null; then
            lkl_test_cmd sudo mkdir /dev/net
            lkl_test_cmd sudo ln -s /dev/tun /dev/net/tun
        fi
        TAP_USER="vpn"
        ANDROID_USER="vpn,vpn,net_admin,inet"
        export_vars ANDROID_USER
    else
        TAP_USER=$USER
    fi
}

tap_setup()
{
    if [ -n "$LKL_HOST_CONFIG_BSD" ]; then
        lkl_test_cmd sudo ifconfig tap create
        lkl_test_cmd sudo sysctl net.link.tap.up_on_open=1
        lkl_test_cmd sudo sysctl net.link.tap.user_open=1
        lkl_test_cmd sudo ifconfig $(tap_ifname) $(ip_host)
        lkl_test_cmd sudo ifconfig $(tap_ifname) inet6 $(ip6_host)
        return
    fi

    lkl_test_cmd sudo ip tuntap add dev $(tap_ifname $1) mode tap user $TAP_USER
    lkl_test_cmd sudo ip link set dev $(tap_ifname $1) up
    lkl_test_cmd sudo ip addr add dev $(tap_ifname $1) $(ip_host_mask $1)
    lkl_test_cmd sudo ip -6 addr add dev $(tap_ifname $1) $(ip6_host_mask $1)

    if [ -n "$LKL_HOST_CONFIG_ANDROID" ]; then
        lkl_test_cmd sudo ip route add $(ip_net_mask $1) \
                     dev $(tap_ifname $1) proto kernel scope link \
                     src $(ip_host $1) table local
        lkl_test_cmd sudo ip -6 route add $(ip6_net_mask $1) \
                     dev $(tap_ifname $1) table local
    fi
}

tap_cleanup()
{
    if [ -n "$LKL_HOST_CONFIG_BSD" ]; then
        lkl_test_cmd sudo ifconfig $(tap_ifname) destroy
        return
    fi

    lkl_test_cmd sudo ip link set dev $(tap_ifname $1) down
    lkl_test_cmd sudo ip tuntap del dev $(tap_ifname $1) mode tap
}
