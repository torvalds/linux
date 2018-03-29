#!/bin/sh

set -e
set -x

if [ $# -ne 1 ]; then
	echo "usage: $0 <block_size>"
	exit 1
fi

block_size=$1

mkdir -p /tmp/pitix.ro
mkdir -p /tmp/pitix.mnt

tar -xzf pitix.files.tar.gz -C /tmp/pitix.ro
./mkfs.pitix $block_size /tmp/pitix.loop

insmod pitix.ko
mount -t pitix /tmp/pitix.loop /tmp/pitix.mnt -o loop

cp -pr /tmp/pitix.ro/* /tmp/pitix.mnt/
ls -lR /tmp/pitix.mnt

umount /tmp/pitix.mnt
rmmod pitix

gzip /tmp/pitix.loop
mv /tmp/pitix.loop.gz .

rm -rf /tmp/pitix.ro
rm -rf /tmp/pitix.mnt

