#!/usr/bin/env bash

script_dir=$(cd $(dirname ${BASH_SOURCE:-$0}); pwd)

clear_wdir()
{
    test -f ${VDESWITCH}.pid && kill $(cat ${VDESWITCH}.pid)
    rm -rf ${wdir}
    tap_cleanup
    tap_cleanup 1
}

set_cfgjson()
{
    cfgjson=${wdir}/hijack-test$1.conf

    if [ -n "$LKL_HOST_CONFIG_ANDROID" ]; then
        adb shell cat \> ${cfgjson}
    else
        cat > ${cfgjson}
    fi

    export_vars cfgjson
}

run_hijack_cfg()
{
    if [ -z "$LKL_HOST_CONFIG_JSMN" ]; then
        echo "no json support"
        exit $TEST_SKIP
    fi

    lkl_test_cmd LKL_HIJACK_CONFIG_FILE=$cfgjson $hijack $@
}

run_hijack()
{
    lkl_test_cmd $hijack $@
}

run_netperf()
{
    lkl_test_cmd TEST_NETSERVER_PORT=$TEST_NETSERVER_PORT \
                 LKL_HIJACK_CONFIG_FILE=$cfgjson $netperf $@
}

test_ping()
{
    set -e

    run_hijack ${ping} -c 1 127.0.0.1
}

test_ping6()
{
    set -e

    run_hijack ${ping6} -c 1 ::1
}

test_mount_and_dump()
{
    set -e

    if [ -n "$LKL_HOST_CONFIG_ANDROID" ]; then
        echo "TODO: android-23 doesn't call destructor..."
        return $TEST_SKIP
    fi

    set_cfgjson << EOF
    {
        "mount":"proc,sysfs",
        "dump":"/sysfs/class/net/lo/mtu,/sysfs/class/net/lo/dev_id",
        "debug": "1"
    }
EOF

    ans=$(run_hijack_cfg $(QUIET=1 lkl_test_cmd which true))
    echo "$ans"
    echo "$ans" | grep "^65536" # lo's MTU
    echo "$ans" | grep "0x0" # lo's dev_id
}

test_boot_cmdline()
{
    set -e

    set_cfgjson << EOF
    {
        "debug":"1",
        "boot_cmdline":"loglevel=1"
    }
EOF

    ans=$(run_hijack_cfg $(QUIET=1 lkl_test_cmd which true))
    echo "$ans"
    [ $(echo "$ans" | wc -l) = 1 ]
}


test_pipe_setup()
{
    set -e

    mkfifo ${fifo1}
    mkfifo ${fifo2}

    set_cfgjson << EOF
    {
        "interfaces":
        [
            {
                "type":"pipe",
                "param":"${fifo1}|${fifo2}",
                "ip":"$(ip_lkl)",
                "masklen":"$TEST_IP_NETMASK",
                "mac":"$TEST_MAC0"
            }
        ]
    }
EOF

    # Make sure our device has the addresses we expect
    addr=$(run_hijack_cfg ip addr)
    echo "$addr" | grep eth0
    echo "$addr" | grep $(ip_lkl)
    echo "$addr" | grep "$TEST_MAC0"
}

test_pipe_ping()
{
    set -e

    set_cfgjson << EOF
    {
        "gateway":"$(ip_lkl)",
        "gateway6":"$(ip6_lkl)",
        "interfaces":
        [
            {
                "type":"pipe",
                "param":"${fifo1}|${fifo2}",
                "ip":"$(ip_host)",
                "masklen":"$TEST_IP_NETMASK",
                "mac":"$TEST_MAC0",
                "ipv6":"$(ip6_host)",
                "masklen6":"$TEST_IP6_NETMASK"
            }
        ]
    }
EOF

    run_hijack_cfg $(QUIET=1 lkl_test_cmd which sleep) 10 &

    set_cfgjson 2 << EOF
    {
        "gateway":"$(ip_host)",
        "gateway6":"$(ip6_host)",
        "interfaces":
        [
            {
                "type":"pipe",
                "param":"${fifo2}|${fifo1}",
                "ip":"$(ip_lkl)",
                "masklen":"$TEST_IP_NETMASK",
                "mac":"$TEST_MAC0",
                "ipv6":"$(ip6_lkl)",
                "masklen6":"$TEST_IP6_NETMASK"
            }
        ]
    }
EOF

    # Ping under LKL
    run_hijack_cfg ${ping} -c 1 -w 10 $(ip_host)

    # Ping 6 under LKL
    run_hijack_cfg ${ping6} -c 1 -w 10 $(ip6_host)

    wait
}

test_tap_setup()
{
    set -e

    # Set up the TAP device we'd like to use
    tap_setup

    set_cfgjson << EOF
    {
        "gateway":"$(ip_host)",
        "gateway6":"$(ip6_host)",
        "debug":"1",
        "interfaces": [
            {
                "type":"tap",
                "param":"$(tap_ifname)",
                "ip":"$(ip_lkl)",
                "masklen":"$TEST_IP_NETMASK",
                "ipv6":"$(ip6_lkl)",
                "masklen6":"$TEST_IP6_NETMASK",
                "mac": "$TEST_MAC0"
            }
        ]
    }
EOF

    # Make sure our device has the addresses we expect
    addr=$(run_hijack_cfg ip addr)
    echo "$addr" | grep eth0
    echo "$addr" | grep $(ip_lkl)
    echo "$addr" | grep "$TEST_MAC0"
    echo "$addr" | grep "$(ip6_lkl)"
    ! echo "$addr" | grep "WARN: failed to free"
}

test_tap_cleanup()
{
    tap_cleanup
    tap_cleanup 1
}

test_tap_ping_host()
{
    set -e

    # Make sure we can ping the host from inside LKL
    run_hijack_cfg ${ping} -c 1 $(ip_host)
    run_hijack_cfg ${ping6} -c 1 $(ip6_host)
}

test_tap_ping_lkl()
{
    # Flush the neighbour cache and without reporting errors since there might
    # not be any entries present.
    lkl_test_cmd sudo ip -6 neigh del $(ip6_lkl) dev $(tap_ifname)
    lkl_test_cmd sudo ip neigh del $(ip_lkl) dev $(tap_ifname)

    # no errors beyond this point
    set -e

    # start LKL and wait a bit so the host can ping
    run_hijack_cfg $(QUIET=1 lkl_test_cmd which sleep) 3 &

    # wait for LKL to boot
    sleep 2

    # check if LKL is alive
    if ! kill -0 $!; then
      wait $!
      exit $?
    fi

    # Now let's check that the host can see LKL.
    lkl_test_cmd sudo ping -i 0.01 -c 65 $(ip_lkl)
    lkl_test_cmd sudo ping6 -i 0.01 -c 65 $(ip6_lkl)
}

test_tap_neighbours()
{
    set -e

    neigh1="$(ip_add 100)|12:34:56:78:9a:bc"
    neigh2="$(ip6_add 100)|12:34:56:78:9a:be"

    set_cfgjson << EOF
    {
        "gateway":"$(ip_host)",
        "gateway6":"$(ip6_host)",
        "interfaces":
        [
            {
                "type":"tap",
                "param":"$(tap_ifname)",
                "ip":"$(ip_lkl)",
                "masklen":"$TEST_IP_NETMASK",
                "ipv6":"$(ip6_lkl)",
                "masklen6":"$TEST_IP6_NETMASK",
                "neigh":"${neigh1};${neigh2}"
            }
        ]
    }
EOF

    # add neighbor entries
    ans=$(run_hijack_cfg ip neighbor show) || true
    echo "$ans" | tail -n 15 | grep "12:34:56:78:9a:bc"
    echo "$ans" | tail -n 15 | grep "12:34:56:78:9a:be"

    # gateway
    ans=$(run_hijack_cfg ip route show) || true
    echo "$ans" | tail -n 15 | grep "$(ip_host)"

    # gateway v6
    ans=$(run_hijack_cfg ip -6 route show) || true
    echo "$ans" | tail -n 15 | grep "$(ip6_host)"
}

test_tap_netperf_stream_tso_csum()
{
    set -e

    # offload
    # LKL_VIRTIO_NET_F_HOST_TSO4 && LKL_VIRTIO_NET_F_GUEST_TSO4
    # LKL_VIRTIO_NET_F_CSUM && LKL_VIRTIO_NET_F_GUEST_CSUM
    set_cfgjson << EOF
    {
        "gateway":"$(ip_host)",
        "gateway6":"$(ip6_host)",
        "interfaces":
        [
            {
                "offload":"0x883",
                "type":"tap",
                "param":"$(tap_ifname)",
                "ip":"$(ip_lkl)",
                "masklen":"$TEST_IP_NETMASK",
                "ipv6":"$(ip6_lkl)",
                "masklen6":"$TEST_IP6_NETMASK"
            }
        ]
    }
EOF

    run_netperf $(ip_host) TCP_STREAM
}

test_tap_netperf_maerts_csum_tso()
{
    run_netperf $(ip_host) TCP_MAERTS
}

test_tap_netperf_stream_csum_tso_mrgrxbuf()
{
    set -e

    # offload
    # LKL_VIRTIO_NET_F_HOST_TSO4 && LKL_VIRTIO_NET_F_MRG_RXBUF
    # LKL_VIRTIO_NET_F_CSUM && LKL_VIRTIO_NET_F_GUEST_CSUM
    set_cfgjson << EOF
    {
        "gateway":"$(ip_host)",
        "gateway6":"$(ip6_host)",
        "interfaces":
        [
            {
                "offload":"0x8803",
                "type":"tap",
                "param":"$(tap_ifname)",
                "ip":"$(ip_lkl)",
                "masklen":"$TEST_IP_NETMASK",
                "ipv6":"$(ip6_lkl)",
                "masklen6":"$TEST_IP6_NETMASK"
            }
        ]
    }
EOF

    run_netperf $(ip_host) TCP_MAERTS
}

test_tap_netperf_tcp_rr()
{
    set -e

    set_cfgjson << EOF
    {
        "gateway":"$(ip_host)",
        "gateway6":"$(ip6_host)",
        "interfaces":
        [
            {
                "type":"tap",
                "param":"$(tap_ifname)",
                "ip":"$(ip_lkl)",
                "masklen":"$TEST_IP_NETMASK",
                "ipv6":"$(ip6_lkl)",
                "masklen6":"$TEST_IP6_NETMASK"
            }
        ]
    }
EOF

    run_netperf $(ip_host) TCP_RR
}

test_tap_netperf_tcp_stream()
{
    set -e

    run_netperf $(ip_host) TCP_STREAM
}

test_tap_netperf_tcp_maerts()
{
    set -e

    run_netperf $(ip_host) TCP_MAERTS
}


test_tap_qdisc()
{
    set -e

    if [ -n "$LKL_HOST_CONFIG_ANDROID" ]; then
        return $TEST_SKIP
    fi

    set_cfgjson << EOF
    {
        "gateway":"$(ip_host)",
        "gateway6":"$(ip6_host)",
        "interfaces":
        [
            {
                "type":"tap",
                "param":"$(tap_ifname)",
                "ip":"$(ip_lkl)",
                "masklen":"$TEST_IP_NETMASK",
                "ipv6":"$(ip6_lkl)",
                "masklen6":"$TEST_IP6_NETMASK",
                "mac":"$TEST_MAC0",
                "qdisc":"root|fq"
            }
        ]
    }
EOF

    qdisc=$(run_hijack_cfg tc -s -d qdisc show)
    echo "$qdisc"
    echo "$qdisc" | grep "qdisc fq" > /dev/null
    echo "$qdisc" | grep throttled > /dev/null
}

test_tap_multi_if_setup()
{
    set -e

    # Set up 2nd TAP device we'd like to use
    tap_setup 1

    set_cfgjson << EOF
    {
        "gateway":"$(ip_host)",
        "gateway6":"$(ip6_host)",
        "interfaces":
        [
            {
                "type":"tap",
                "param":"$(tap_ifname)",
                "ip":"$(ip_lkl)",
                "masklen":"$TEST_IP_NETMASK",
                "ipv6":"$(ip6_lkl)",
                "masklen6":"$TEST_IP6_NETMASK",
                "mac":"$TEST_MAC0"
            },
            {
                "type":"tap",
                "param":"$(tap_ifname 1)",
                "ip":"$(ip_lkl 1)",
                "masklen":"$TEST_IP_NETMASK",
                "ipv6":"$(ip6_lkl 1)",
                "masklen6":"$TEST_IP6_NETMASK",
                "mac":"$TEST_MAC1"
            }
        ]
    }
EOF

    # Make sure our device has the addresses we expect
    addr=$(run_hijack_cfg ip addr)
    echo "$addr" | grep eth0
    echo "$addr" | grep $(ip_lkl)
    echo "$addr" | grep "$TEST_MAC0"
    echo "$addr" | grep "$(ip6_lkl)"
    echo "$addr" | grep eth1
    echo "$addr" | grep $(ip_lkl 1)
    echo "$addr" | grep "$TEST_MAC1"
    echo "$addr" | grep "$(ip6_lkl 1)"
    ! echo "$addr" | grep "WARN: failed to free"
}

test_tap_multi_if_ping()
{
    run_hijack_cfg ${ping} -c 1 $(ip_host)
    run_hijack_cfg ${ping6} -c 1 $(ip6_host)
    run_hijack_cfg ${ping} -c 1 $(ip_host 1)
    run_hijack_cfg ${ping6} -c 1 $(ip6_host 1)
}

test_tap_multi_if_neigh()
{

    neigh1="$(ip_host)00|12:34:56:78:9a:bc"
    neigh2="$(ip6_host)00|12:34:56:78:9a:be"
    neigh3="$(ip_host 1)00|12:34:56:78:9a:bd"
    neigh4="$(ip6_host 1)00|12:34:56:78:9a:bf"

    set_cfgjson << EOF
    {
        "gateway":"$(ip_host)",
        "gateway6":"$(ip6_host)",
        "interfaces":
        [
            {
                "type":"tap",
                "param":"$(tap_ifname)",
                "ip":"$(ip_lkl)",
                "masklen":"$TEST_IP_NETMASK",
                "ipv6":"$(ip6_lkl)",
                "masklen6":"$TEST_IP6_NETMASK",
                "mac":"$TEST_MAC0",
                "neigh":"${neigh1};${neigh2}"
            },
            {
                "type":"tap",
                "param":"$(tap_ifname 1)",
                "ip":"$(ip_lkl 1)",
                "masklen":"$TEST_IP_NETMASK",
                "ipv6":"$(ip6_lkl 1)",
                "masklen6":"$TEST_IP6_NETMASK",
                "mac":"$TEST_MAC1",
                "neigh":"${neigh3};${neigh4}"
            }
        ]
    }
EOF

    # add neighbor entries
    ans=$(run_hijack_cfg ip neighbor show) || true
    echo "$ans" | tail -n 15 | grep "12:34:56:78:9a:bc"
    echo "$ans" | tail -n 15 | grep "12:34:56:78:9a:be"
    echo "$ans" | tail -n 15 | grep "12:34:56:78:9a:bd"
    echo "$ans" | tail -n 15 | grep "12:34:56:78:9a:bf"
}

test_tap_multi_if_gateway()
{
    ans=$(run_hijack_cfg ip route show) || true
    echo "$ans" | tail -n 15 | grep "$(ip_host)"
}

test_tap_multi_if_gateway_v6()
{
    ans=$(run_hijack_cfg ip -6 route show) || true
    echo "$ans" | tail -n 15 | grep "$(ip6_host)"
}


test_tap_multitable_setup()
{
    set_cfgjson << EOF
    {
        "gateway":"$(ip_host)",
        "gateway6":"$(ip6_host)",
        "interfaces":
        [
            {
                "type":"tap",
                "param":"$(tap_ifname)",
                "ip":"$(ip_lkl)",
                "masklen":"$TEST_IP_NETMASK",
                "ifgateway":"$(ip_host)",
                "ipv6":"$(ip6_lkl)",
                "masklen6":"$TEST_IP6_NETMASK",
                "ifgateway6":"$(ip6_host)",
                "mac":"$TEST_MAC0",
                "neigh":"${neigh1};${neigh2}"
            },
            {
                "type":"tap",
                "param":"$(tap_ifname 1)",
                "ip":"$(ip_lkl 1)",
                "masklen":"$TEST_IP_NETMASK",
                "ifgateway":"$(ip_host 1)",
                "ipv6":"$(ip6_lkl 1)",
                "masklen6":"$TEST_IP6_NETMASK",
                "ifgateway6":"$(ip6_host 1)",
                "mac":"$TEST_MAC1",
                "neigh":"${neigh3};${neigh4}"
            }
        ]
    }
EOF
}

test_tap_multitable_ipv4_rule()
{
    addr=$(run_hijack_cfg ip rule show)
    echo "$addr" | grep $(ip_lkl)
    echo "$addr" | grep $(ip_lkl 1)
}

test_tap_multitable_ipv6_rule()
{
    addr=$(run_hijack_cfg ip -6 rule show)
    echo "$addr" | grep $(ip6_lkl)
    echo "$addr" | grep $(ip6_lkl 1)
}

test_tap_multitable_ipv4_rule_table_4()
{
    addr=$(run_hijack_cfg ip route show table 4)
    echo "$addr" | grep $(ip_host)
}

test_tap_multitable_ipv6_rule_table_5()
{
    addr=$(run_hijack_cfg ip -6 route show table 5)
    echo "$addr" | grep fc03::
    echo "$addr" | grep $(ip6_host)
}

test_tap_multitable_ipv6_rule_table_6()
{
    addr=$(run_hijack_cfg ip route show table 6)
    echo "$addr" | grep $(ip_host 1)
}

test_tap_multitable_ipv6_rule_table_7()
{
    addr=$(run_hijack_cfg ip -6 route show table 7)
    echo "$addr" | grep fc04::
    echo "$addr" | grep $(ip6_host 1)
}

test_vde_setup()
{
    set_cfgjson << EOF
    {
        "gateway":"$(ip_host)",
        "gateway6":"$(ip6_host)",
        "interfaces":
        [
            {
                "type":"vde",
                "param":"${VDESWITCH}",
                "ip":"$(ip_lkl)",
                "masklen":"$TEST_IP_NETMASK",
                "ipv6":"$(ip6_lkl)",
                "masklen6":"$TEST_IP6_NETMASK",
                "mac":"$TEST_MAC0",
                "neigh":"${neigh1};${neigh2}"
            }
        ]
    }
EOF

    tap_setup

    sleep 2
    vde_switch -d -t $(tap_ifname) -s ${VDESWITCH} -p ${VDESWITCH}.pid

    # Make sure our device has the addresses we expect
    addr=$(run_hijack_cfg ip addr)
    echo "$addr" | grep eth0
    echo "$addr" | grep $(ip_lkl)
    echo "$addr" | grep "$TEST_MAC0"
}

test_vde_cleanup()
{
    tap_cleanup
}

test_vde_ping_host()
{
    run_hijack_cfg ./ping $(ip_host) -c 1
}

test_vde_ping_lkl()
{
    lkl_test_cmd sudo arp -d $(ip_lkl)
    lkl_test_cmd sudo ping -i 0.01 -c 65 $(ip_lkl) &
    run_hijack_cfg sleep 3
}

source ${script_dir}/test.sh
source ${script_dir}/net-setup.sh

if [[ ! -e ${basedir}/lib/hijack/liblkl-hijack.so ]]; then
    lkl_test_plan 0 "hijack tests"
    echo "missing liblkl-hijack.so"
    exit 0
fi

if [ -n "${LKL_HIJACK_ZPOLINE}" ]
then
    if [ -z "$LKL_HOST_CONFIG_ZPOLINE_DIR" ];  then
       lkl_test_plan 0 "zpoline tests"
       echo "missing zpoline configuration"
       exit $TEST_SKIP
    fi
    test_header=" (zpoline)"
fi


if [ -n "$LKL_HOST_CONFIG_ANDROID" ]; then
    wdir=$ANDROID_WDIR
    adb_push lib/hijack/liblkl-hijack.so bin/lkl-hijack.sh tests/net-setup.sh \
             tests/run_netperf.sh tests/hijack-test.sh tests/autoconf.sh
    ping="ping"
    ping6="ping6"
    hijack="$wdir/bin/lkl-hijack.sh"
    netperf="$wdir/tests/run_netperf.sh"
else
    # Make a temporary directory to run tests in, since we'll be copying
    # things there.
    wdir=$(mktemp -d)
    cp `which ping` ${wdir}
    cp `which ping6` ${wdir}
    ping=${wdir}/ping
    ping6=${wdir}/ping6
    hijack=$basedir/bin/lkl-hijack.sh
    netperf=$basedir/tests/run_netperf.sh
fi

fifo1=${wdir}/fifo1
fifo2=${wdir}/fifo2
VDESWITCH=${wdir}/vde_switch

# And make sure we clean up when we're done
trap "clear_wdir &>/dev/null" EXIT

lkl_test_plan 5 "hijack basic tests${test_header}"
lkl_test_run 1 run_hijack ip addr
lkl_test_run 2 run_hijack ip route
lkl_test_run 3 test_ping
lkl_test_run 4 test_ping6
lkl_test_run 5 test_mount_and_dump
lkl_test_run 6 test_boot_cmdline

if [ -z "$(QUIET=1 lkl_test_cmd which mkfifo)" ]; then
    lkl_test_plan 0 "hijack pipe backend tests"
    echo "no mkfifo command"
else
    lkl_test_plan 2 "hijack pipe backend tests"
    lkl_test_run 1 test_pipe_setup
    lkl_test_run 2 test_pipe_ping
fi

tap_prepare

if ! lkl_test_cmd test -c /dev/net/tun &>/dev/null; then
    lkl_test_plan 0 "hijack tap backend tests"
    echo "missing /dev/net/tun"
elif [ -z "$LKL_HOST_CONFIG_JSMN" ]; then
    lkl_test_plan 0 "hijack tap backend tests"
    echo "no json support"
else
    lkl_test_plan 23 "hijack tap backend tests"
    lkl_test_run 1 test_tap_setup
    lkl_test_run 2 test_tap_ping_host
    lkl_test_run 3 test_tap_ping_lkl
    lkl_test_run 4 test_tap_neighbours
    lkl_test_run 5 test_tap_netperf_tcp_rr
    lkl_test_run 6 test_tap_netperf_tcp_stream
    lkl_test_run 7 test_tap_netperf_tcp_maerts
    lkl_test_run 8 test_tap_netperf_stream_tso_csum
    lkl_test_run 9 test_tap_netperf_maerts_csum_tso
    lkl_test_run 10 test_tap_netperf_stream_csum_tso_mrgrxbuf
    lkl_test_run 11 test_tap_qdisc
    lkl_test_run 12 test_tap_multi_if_setup
    lkl_test_run 13 test_tap_multi_if_ping
    lkl_test_run 14 test_tap_multi_if_neigh
    lkl_test_run 15 test_tap_multi_if_gateway
    lkl_test_run 16 test_tap_multi_if_gateway_v6
    lkl_test_run 17 test_tap_multitable_setup
    lkl_test_run 18 test_tap_multitable_ipv4_rule
    lkl_test_run 19 test_tap_multitable_ipv6_rule
    lkl_test_run 20 test_tap_multitable_ipv4_rule_table_4
    lkl_test_run 21 test_tap_multitable_ipv6_rule_table_5
    lkl_test_run 22 test_tap_multitable_ipv6_rule_table_6
    lkl_test_run 23 test_tap_multitable_ipv6_rule_table_7
    lkl_test_run 24 test_tap_cleanup
fi

if [ -z "$LKL_HOST_CONFIG_VIRTIO_NET_VDE" ]; then
    lkl_test_plan 0 "vde tests"
    echo "vde not supported"
elif [ ! -x "$(which vde_switch)" ]; then
    lkl_test_plan 0 "hijack vde tests"
    echo "could not find a vde_switch executable"
else
    lkl_test_plan 3 "hijack vde tests"
    lkl_test_run 1 test_vde_setup
    lkl_test_run 2 test_vde_ping_host
    lkl_test_run 3 test_vde_ping_lkl
    lkl_test_run 4 test_vde_cleanup
fi
