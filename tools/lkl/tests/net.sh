#!/usr/bin/env bash

script_dir=$(cd $(dirname ${BASH_SOURCE:-$0}); pwd)

source $script_dir/test.sh
source $script_dir/net-setup.sh

cleanup_backend()
{
    set -e

    case "$1" in
    "tap")
        tap_cleanup
        ;;
    "pipe")
        rm -rf $work_dir
        ;;
    "raw")
        ;;
    "macvtap")
        sudo ip link del dev $(tap_ifname) type macvtap
        ;;
    "loopback")
        ;;
    "wintap")
        ;;
    esac
}

get_test_ip()
{
    # skip if we don't explicitly allow testing on live host network
    if [ -z "$LKL_TEST_ON_LIVE_HOST_NETWORK" ]; then
        echo "LKL_TEST_ON_LIVE_HOST_NETWORK is not set, skipping test"
        return $TEST_SKIP
    fi
    # DHCP test parameters
    TEST_HOST=8.8.8.8
    HOST_IF=$(lkl_test_cmd ip route get $TEST_HOST | head -n1 |cut -d ' ' -f5)
    HOST_GW=$(lkl_test_cmd ip route get $TEST_HOST | head -n1 | cut -d ' ' -f3)
    if lkl_test_cmd ping -c1 -w1 $HOST_GW; then
        TEST_IP_REMOTE=$HOST_GW
    elif lkl_test_cmd ping -c1 -w1 $TEST_HOST; then
        TEST_IP_REMOTE=$TEST_HOST
    else
        echo "could not find remote test ip"
        return $TEST_SKIP
    fi

    export_vars HOST_IF TEST_IP_REMOTE
}

setup_backend()
{
    set -e

    if [ "$LKL_HOST_CONFIG_POSIX" != "y" ] &&
           [ "$LKL_HOST_CONFIG_NT" != "y" ] &&
           [ "$1" != "loopback" ]; then
        echo "not a posix or windows environment"
        return $TEST_SKIP
    fi

    case "$1" in
    "loopback")
        ;;
    "pipe")
        if [ -n "$LKL_HOST_CONFIG_NT" ]; then
            return $TEST_SKIP
        fi
        if [ -z $(lkl_test_cmd which mkfifo) ]; then
            echo "no mkfifo command"
            return $TEST_SKIP
        else
            work_dir=$(lkl_test_cmd mktemp -d)
        fi
        fifo1=$work_dir/fifo1
        fifo2=$work_dir/fifo2
        lkl_test_cmd mkfifo $fifo1
        lkl_test_cmd mkfifo $fifo2
        export_vars work_dir fifo1 fifo2
        ;;
    "tap")
        tap_prepare
        if ! lkl_test_cmd test -c /dev/net/tun; then
            if [ -z "$LKL_HOST_CONFIG_BSD" ]; then
                echo "missing /dev/net/tun"
                return $TEST_SKIP
            fi
        fi
        tap_setup
        ;;
    "raw")
        if [ -n "$LKL_HOST_CONFIG_BSD" ]; then
            return $TEST_SKIP
        fi
        get_test_ip
        ;;
    "macvtap")
        get_test_ip
        if ! lkl_test_cmd sudo ip link add link $HOST_IF \
             name $(tap_ifname) type macvtap mode passthru; then
            echo "failed to create macvtap, skipping"
            return $TEST_SKIP
        fi
        MACVTAP=/dev/tap$(lkl_test_cmd ip link show dev $(tap_ifname) | \
                                 grep -o ^[0-9]*)
        lkl_test_cmd sudo ip link set dev $(tap_ifname) up
        lkl_test_cmd sudo chown $USER $MACVTAP
        export_vars MACVTAP
        ;;
    "dpdk")
        if -z [ $LKL_TEST_NET_DPDK ]; then
            echo "DPDK needs user setup"
            return $TEST_SKIP
        fi
        ;;
    "wintap")
        if [ -z $LKL_HOST_CONFIG_NT64 ]; then
            echo "skipping on non-windows 64-bits host"
            return $TEST_SKIP
        fi
        netsh interface ip set address \
	      "OpenVPN TAP-Windows6" static $(ip_host) 255.255.255.0 0.0.0.0
        netsh advfirewall set allprofiles state off
        ;;
    *)
        echo "don't know how to setup backend $1"
        return $TEST_FAILED
        ;;
    esac
}

run_tests()
{
    case "$1" in
    "loopback")
        lkl_test_exec $script_dir/net-test --dst 127.0.0.1
        ;;
    "pipe")
        VALGRIND="" lkl_test_exec $script_dir/net-test --backend pipe \
                      --ifname "$fifo1|$fifo2" \
                      --ip $(ip_host) --netmask-len $TEST_IP_NETMASK \
                      --sleep 1800 >/dev/null &
        cp $script_dir/net-test $script_dir/net-test2

        sleep 10
        lkl_test_exec $script_dir/net-test2 --backend pipe \
                      --ifname "$fifo2|$fifo1" \
                      --ip $(ip_lkl) --netmask-len $TEST_IP_NETMASK \
                      --dst $(ip_host)
        rm -f $script_dir/net-test2
        kill $!
        wait $! 2>/dev/null
        ;;
    "tap")
        lkl_test_exec $script_dir/net-test --backend tap \
                      --ifname $(tap_ifname) \
                      --ip $(ip_lkl) --netmask-len $TEST_IP_NETMASK \
                      --dst $(ip_host)
        ;;
    "raw")
        lkl_test_exec sudo $script_dir/net-test --backend raw \
                      --ifname $HOST_IF --dhcp --dst $TEST_IP_REMOTE
        ;;
    "macvtap")
        lkl_test_exec $script_dir/net-test --backend macvtap \
                      --ifname $MACVTAP \
                      --dhcp --dst $TEST_IP_REMOTE
        ;;
    "dpdk")
        lkl_test_exec sudo $script_dir/net-test --backend dpdk \
                      --ifname dpdk0 \
                      --ip $(ip_lkl) --netmask-len $TEST_IP_NETMASK \
                      --dst $(ip_host)
        ;;
    "wintap")
        lkl_test_exec $script_dir/net-test --backend wintap \
                      --ifname tap0 \
                      --ip $(ip_lkl) --netmask-len $TEST_IP_NETMASK \
                      --dst $(ip_host) --sleep 10
        ;;
    esac
}

if [ "$1" = "-b" ]; then
    shift
    backend=$1
    shift
fi

if [ -z "$backend" ]; then
    backend="loopback"
fi

lkl_test_plan 1 "net $backend"
lkl_test_run 1 setup_backend $backend

if [ $? = $TEST_SKIP ]; then
    exit 0
fi

trap "cleanup_backend $backend" EXIT

run_tests $backend

trap : EXIT
lkl_test_plan 1 "net $backend"
lkl_test_run 1 cleanup_backend $backend

