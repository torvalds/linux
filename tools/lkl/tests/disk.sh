#!/bin/bash

set -e

script_dir=$(cd $(dirname ${BASH_SOURCE:-$0}); pwd)

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

if file $script_dir/disk | grep PE32; then
    WRAP=wine
elif file $script_dir/disk | grep ARM; then
    WRAP=qemu-arm-static
fi


${TEST_CMD} $WRAP $script_dir/disk -d $file -t $fstype $LKL_TEST_DEBUG $@ || \
    err=$?

rm $file || true

exit $err
