#!/bin/bash -e

script_dir=$(cd $(dirname ${BASH_SOURCE:-$0}); pwd)
hijack_script=lkl-hijack.sh
export PATH=${script_dir}/../bin/:${PATH}

if ! [ -e ${script_dir}/../lib/liblkl-hijack.so ]; then
    exit 0;
fi

echo "== ip addr test=="
${hijack_script} ip addr

echo "== ip route test=="
${hijack_script} ip route

echo "== ping test=="
cp `which ping` .
${script_dir}/../bin/${hijack_script} ./ping 127.0.0.1 -c 2
rm ping

echo "== ping6 test=="
cp `which ping6` .
${script_dir}/../bin/${hijack_script} ./ping6 ::1 -c 2
rm ping6

echo "== TAP tests =="
if [ -c /dev/net/tun ]; then
    sudo ip link set dev lkl_ptt0 down || true
    sudo ip tuntap del dev lkl_ptt0 mode tap || true
    sudo ip tuntap add dev lkl_ptt0 mode tap user $USER
    sudo ip link set dev lkl_ptt0 up
    sudo ip addr add dev lkl_ptt0 192.168.13.1/24
    LKL_HIJACK_NET_TAP=lkl_ptt0 LKL_HIJACK_NET_IP=192.168.13.2 LKL_HIJACK_NET_NETMASK_LEN=24 ${hijack_script} ip link | grep eth0
    LKL_HIJACK_NET_TAP=lkl_ptt0 LKL_HIJACK_NET_IP=192.168.13.2 LKL_HIJACK_NET_NETMASK_LEN=24 ${hijack_script} ip addr | grep 192.168.13.2
    cp `which ping` .
    LKL_HIJACK_NET_TAP=lkl_ptt0 LKL_HIJACK_NET_IP=192.168.13.2 LKL_HIJACK_NET_NETMASK_LEN=24 ${hijack_script} ./ping 192.168.13.1 -i 0.2 -c 65
    rm ./ping
    (sudo arp -d 192.168.13.2 && ping -i 0.2 -c 65 192.168.13.2 & LKL_HIJACK_NET_TAP=lkl_ptt0 LKL_HIJACK_NET_IP=192.168.13.2 LKL_HIJACK_NET_NETMASK_LEN=24 ${hijack_script} sleep 15)
    sudo ip link set dev lkl_ptt0 down
    sudo ip tuntap del dev lkl_ptt0 mode tap
fi;
