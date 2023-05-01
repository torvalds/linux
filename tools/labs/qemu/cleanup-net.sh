#!/bin/bash

DNSMASQ=/tmp/dnsmasq

for i in lkt-tap0 lkt-tap1 lkt-tap-smbd; do
  if ! ip a s dev &> /dev/null $i; then
    continue
  fi
  if [ -f $DNSMASQ-$i.pid ]; then
    sudo kill `cat $DNSMASQ-$i.pid`
  fi
  sudo rm $DNSMASQ-$i.leases
  sudo ip tuntap del $i mode tap
done
