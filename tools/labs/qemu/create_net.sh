#!/bin/bash

if test $# -ne 1; then
    echo "Usage: $0 <device>" 1>&2
    echo " <device> must be tap0 or tap1"
    exit 1
fi

device=$1

USER=$(whoami)

case "$device" in
    "tap0")
        subnet=172.213.0
        ;;
    "tap1")
        subnet=172.30.0
        ;;
    *)
        echo "Unknown device" 1>&2
        exit 1
        ;;
esac

# If device doesn't exist add device.
if ! /sbin/ip link show dev "$device" > /dev/null 2>&1; then
    sudo ip tuntap add mode tap user "$USER" dev "$device"
fi

# Reconfigure just to be sure (even if device exists).
sudo /sbin/ip address flush dev "$device"
sudo /sbin/ip link set dev "$device" down
sudo /sbin/ip address add $subnet.1/24 dev "$device"
sudo /sbin/ip link set dev "$device" up

mkdir -p $PWD/tftp

sudo dnsmasq --enable-tftp --tftp-root=$PWD/tftp --no-resolv --no-hosts --bind-interfaces --interface $device -F $subnet.2,$subnet.20 -x dnsmasq.pid || true
