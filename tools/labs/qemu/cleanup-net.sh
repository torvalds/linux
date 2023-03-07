#!/bin/bash

DNSMASQ=/tmp/dnsmasq

for i in lkt-tap0 lkt-tap1; do
  if [ $DNSMASQ-$i.pid ]; then
    sudo kill `cat $DNSMASQ-$i.pid`
  fi
  sudo rm $DNSMASQ-$i.leases
  sudo ip tuntap del $i mode tap
done
