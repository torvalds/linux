#!/bin/bash -e

IFNAME=`ip route |grep default | awk '{print $5}'`
GW=`ip route |grep default | awk '{print $3}'`
HOST_IPADDR=`ip rou |grep ${IFNAME} | grep "scope link" | awk '{print $1}' | sed "s/\(.*\)\/.*/\1/"`
PLEN=`ip rou |grep ${IFNAME} | grep "scope link" | awk '{print $1}' | sed "s/.*\/\(.*\)/\1/"`
IPADDR=`echo ${HOST_IPADDR}|awk -F. '{printf ("%d.%d.%d.%d\n",$1,$2,$3,$4+10)}'`

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

echo "== RAW socket (LKL net) tests =="
# currently not supported mingw
if [ -n "`printenv CONFIG_AUTO_LKL_POSIX_HOST`" ] ; then
    sudo ip link set dev ${IFNAME} promisc on
    # this won't work if IFNAME is wifi since it rewrites the src macaddr
    sudo ./net-test raw ${IFNAME} 8.8.8.8 ${IPADDR} ${PLEN} ${GW}

    # DHCP test
    echo "  == DHCP with RAW socket test =="
    sudo ./net-test raw ${IFNAME} 8.8.8.8 dhcp
    sudo ip link set dev ${IFNAME} promisc off
fi

echo "== macvtap (LKL net) tests =="
# currently not supported mingw
sudo ip link add link ${IFNAME} name lkl_vtap0 type macvtap mode passthru
if ls /dev/tap* > /dev/null 2>&1 ; then
    sudo ip link set dev lkl_vtap0 up
    sudo chown ${USER} `ls /dev/tap*`

    ./net-test macvtap `ls /dev/tap*` 8.8.8.8 ${IPADDR} ${PLEN} ${GW}
fi

# we disabled this DPDK test because it's unlikely possible to describe
# a generic set of commands for all environments to test with DPDK.  users
# may customize those test commands for your host.
if false ; then
    echo "== DPDK (LKL net) tests =="
    sudo ./net-test dpdk dpdk0 192.168.15.1 192.168.15.2 24
fi
