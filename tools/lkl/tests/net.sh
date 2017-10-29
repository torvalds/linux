#!/bin/bash

set -e

# android doesn't have sudo
if [ -z ${LKL_ANDROID_TEST} ] ; then
    SUDO="sudo"
fi

TEST_HOST=8.8.8.8
IFNAME=`ip route get ${TEST_HOST} |head -n1 | cut -d ' ' -f5`
GW=`ip route get ${TEST_HOST} |head -n1 | cut -d ' ' -f3`

script_dir=$(cd $(dirname ${BASH_SOURCE:-$0}); pwd)
cd ${script_dir}

# Make a temporary directory to run tests in, since we'll be copying
# things there.
work_dir=$(mktemp -d)

# And make sure we clean up when we're done
function clear_work_dir {
    rm -rf ${work_dir}
    ${SUDO} ip link set dev lkl_ptt1 down &> /dev/null || true
    ${SUDO} ip tuntap del dev lkl_ptt1 mode tap &> /dev/null || true
    ${SUDO} ip link del dev lkl_vtap0 type macvtap &> /dev/null || true
}

trap clear_work_dir EXIT

echo "== Loopback (LKL net) tests =="
./net-test --dst 127.0.0.1

# the rest of the tests are not supported on mingw
if [ "`printenv LKL_CONFIG_POSIX_HOST`" != "y" ] ; then
    exit 0
fi

echo "== PIPE (LKL net) tests =="
if [ -z `which mkfifo` ]; then
    echo "WARNIG: no mkfifo command, skipping PIPE tests."
else

fifo1=${work_dir}/fifo1
fifo2=${work_dir}/fifo2
mkfifo ${fifo1}
mkfifo ${fifo2}
hijack_script=${script_dir}/../bin/lkl-hijack.sh
LKL_HIJACK_NET_IFTYPE=pipe \
	LKL_HIJACK_NET_IFPARAMS="${fifo1}|${fifo2}" \
	LKL_HIJACK_NET_IP=192.168.16.1 \
	LKL_HIJACK_NET_NETMASK_LEN=24 \
	${hijack_script} sleep 10 &
sleep 5
./net-test --backend pipe --ifname "${fifo2}|${fifo1}" --ip 192.168.16.2 \
           --netmask-len 24 --dst 192.168.16.1
wait
fi

echo "== TAP (LKL net) tests =="
if [ -c /dev/net/tun ]; then
    ${SUDO} ip link set dev lkl_ptt1 down || true
    ${SUDO} ip tuntap del dev lkl_ptt1 mode tap || true
    ${SUDO} ip tuntap add dev lkl_ptt1 mode tap user $USER
    ${SUDO} ip link set dev lkl_ptt1 up
    ${SUDO} ip addr add dev lkl_ptt1 192.168.14.1/24

    ./net-test --backend tap --ifname lkl_ptt1 --ip 192.168.14.2 \
               --netmask-len 24 --dst 192.168.14.1

    ${SUDO} ip link set dev lkl_ptt1 down
    ${SUDO} ip tuntap del dev lkl_ptt1 mode tap
fi

if ping -c1 -w1 $GW &>/dev/null; then
    DST=$GW
elif ping -c1 -w1 ${TEST_HOST} &>/dev/null; then
    DST=${TEST_HOST}
fi

if [ -z $LKL_TEST_DHCP ] ; then
    echo "\$LKL_TEST_DHCP is not configured. skipped dhcp client test"
else
if ! [ -z $DST ]; then
    echo "== RAW socket (LKL net) tests =="
    ${SUDO} ip link set dev ${IFNAME} promisc on
    ${SUDO} ./net-test --backend raw --ifname ${IFNAME} --dhcp --dst ${DST}
    ${SUDO} ip link set dev ${IFNAME} promisc off

    echo "== macvtap (LKL net) tests =="
    ${SUDO} ip link add link ${IFNAME} name lkl_vtap0 \
	    type macvtap mode passthru || true
    if ls /dev/tap* > /dev/null 2>&1 ; then
	${SUDO} ip link set dev lkl_vtap0 up
	${SUDO} chown ${USER} `ls /dev/tap*`
	./net-test --backend macvtap --ifname `ls /dev/tap*` --dhcp --dst $DST
    fi
fi
fi

# we disabled this DPDK test because it's unlikely possible to describe
# a generic set of commands for all environments to test with DPDK.  users
# may customize those test commands for your host.
if false ; then
    echo "== DPDK (LKL net) tests =="
    ${SUDO} ./net-test --backend dpdk --ifname dpdk0 --ip 192.168.15.1 \
            --netmask-len 24 --dst 192.168.15.2
fi
