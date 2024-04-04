#!/bin/bash

DNSMASQ=/tmp/dnsmasq

for device in lkt-tap0 lkt-tap1 lkt-tap-smbd; do
  if ! ip a s dev &> /dev/null $device; then
    continue
  fi
  if [ -f $DNSMASQ-$device.pid ]; then
    sudo kill $(cat $DNSMASQ-$device.pid)
  fi
  sudo rm $DNSMASQ-$device.leases
  if [ -e $(which --skip-alias firewall-cmd) ]; then
    sudo firewall-cmd --zone=trusted --remove-interface=$device
  fi
  sudo ip tuntap del $device mode tap
done
