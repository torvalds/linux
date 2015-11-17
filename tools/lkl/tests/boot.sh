#!/bin/bash -e

if [ "$1" = "-t" ]; then
    shift
    fstype=$1
    shift
fi

if [ -z "$fstype" ]; then
    fstype="ext4"
fi

file=`mktemp`
dd if=/dev/zero of=$file bs=1024 count=20480

yes | mkfs.$fstype $file >/dev/null

./boot -d $file -t $fstype $@

rm $file
