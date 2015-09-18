#!/bin/bash

file=`mktemp`
dd if=/dev/zero of=$file bs=1024 count=10240

yes | mkfs.ext4 -q $file

./boot -d $file $@

rm $file
