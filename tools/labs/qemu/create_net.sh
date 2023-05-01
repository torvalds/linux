#!/bin/bash

if test $# -ne 1; then
    echo "Usage: $0 <device>" 1>&2
    echo " <device> must be lkt-tap0 or lkt-tap1"
    exit 1
fi

device=$1

USER=$(whoami)

case "$device" in
    "lkt-tap0")
        subnet=172.213.0
        ;;
    "lkt-tap1")
        subnet=172.30.0
        ;;
    "lkt-tap-smbd")
        subnet=10.0.2
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

sudo dnsmasq --port=0 --no-resolv --no-hosts --bind-interfaces \
  --interface $device -F $subnet.2,$subnet.20 --listen-address $subnet.1 \
  -x /tmp/dnsmasq-$device.pid -l /tmp/dnsmasq-$device.leases || true
