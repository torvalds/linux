#!/bin/bash -e

# currently not supported mingw
if [ "`printenv CONFIG_AUTO_LKL_POSIX_HOST`" != "y" ] ; then
    exit 0
fi

IFNAME=`ip route |grep default | awk '{print $5}' | head -n1`
GW=`ip route |grep default | awk '{print $3}' | head -n1`

script_dir=$(cd $(dirname ${BASH_SOURCE:-$0}); pwd)
cd ${script_dir}

# And make sure we clean up when we're done
function clear_work_dir {
    sudo ip link set dev lkl_ptt1 down &> /dev/null || true
    sudo ip tuntap del dev lkl_ptt1 mode tap &> /dev/null || true
    sudo ip link del dev lkl_vtap0 type macvtap &> /dev/null || true
}

trap clear_work_dir EXIT

echo "== TAP (LKL net) tests =="
if [ -c /dev/net/tun ]; then
    sudo ip link set dev lkl_ptt1 down || true
    sudo ip tuntap del dev lkl_ptt1 mode tap || true
    sudo ip tuntap add dev lkl_ptt1 mode tap user $USER
    sudo ip link set dev lkl_ptt1 up
    sudo ip addr add dev lkl_ptt1 192.168.14.1/24

    ./net-test tap lkl_ptt1 192.168.14.1 192.168.14.2 24

    sudo ip link set dev lkl_ptt1 down
    sudo ip tuntap del dev lkl_ptt1 mode tap
fi

if ping -c1 -w1 $GW &>/dev/null; then
    DST=$GW
elif ping -c1 -w1 8.8.8.8 &>/dev/null; then
    DST=8.8.8.8
fi

if [ -z $LKL_TEST_DHCP ] ; then
    echo "\$LKL_TEST_DHCP is not configured. skipped dhcp client test"
else
if ! [ -z $DST ]; then
    echo "== RAW socket (LKL net) tests =="
    sudo ip link set dev ${IFNAME} promisc on
    sudo ./net-test raw ${IFNAME} ${DST} dhcp
    sudo ip link set dev ${IFNAME} promisc off

    echo "== macvtap (LKL net) tests =="
    sudo ip link add link ${IFNAME} name lkl_vtap0 type macvtap mode passthru
    if ls /dev/tap* > /dev/null 2>&1 ; then
	sudo ip link set dev lkl_vtap0 up
	sudo chown ${USER} `ls /dev/tap*`
	./net-test macvtap `ls /dev/tap*` $DST dhcp
    fi
fi
fi

# we disabled this DPDK test because it's unlikely possible to describe
# a generic set of commands for all environments to test with DPDK.  users
# may customize those test commands for your host.
if false ; then
    echo "== DPDK (LKL net) tests =="
    sudo ./net-test dpdk dpdk0 192.168.15.1 192.168.15.2 24
fi
