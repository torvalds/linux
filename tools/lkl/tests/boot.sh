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

if file ./boot | grep PE32; then
    WRAP=wine
elif file ./boot | grep ARM; then
    WRAP=qemu-arm-static
fi


${TEST_CMD} $WRAP ./boot -d $file -t $fstype $LKL_TEST_DEBUG $@ || err=$?

rm $file || true

exit $err
