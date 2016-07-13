#!/bin/bash

set -e

if [ "$1" = "-t" ]; then
    shift
    fstype=$1
    shift
fi

if [ -z "$fstype" ]; then
    fstype="ext4"
fi

file=`mktemp`
dd if=/dev/zero of=$file bs=1024 count=102400

yes | mkfs.$fstype $file >/dev/null

if [ -c /dev/net/tun ]; then
    sudo ip tuntap del dev lkl_boot mode tap || true
    sudo ip tuntap add dev lkl_boot mode tap user $USER
    tap_args="-n lkl_boot"
fi;

if file ./boot | grep PE32; then
    WRAP=wine
elif file ./boot | grep ARM; then
    WRAP=qemu-arm-static
fi


${TEST_CMD} $WRAP ./boot -d $file -t $fstype $tap_args $LKL_TEST_DEBUG $@ || err=$?

if [ -c /dev/net/tun ]; then
    sudo ip tuntap del dev lkl_boot mode tap || true
fi;

rm $file || true

exit $err
