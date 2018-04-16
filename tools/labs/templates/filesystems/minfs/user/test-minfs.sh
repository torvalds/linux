#!/bin/sh

set -ex

#load module
insmod ../kernel/minfs.ko

#create mount point
mkdir -p /mnt/minfs

#format partition
./mkfs.minfs /dev/vdb

#mount filesystem
mount -t minfs /dev/vdb /mnt/minfs

#show registered filesystems
cat /proc/filesystems | grep minfs

#show mounted filesystems
cat /proc/mounts | grep minfs

#show filesystem statistics
stat -f /mnt/minfs

#list all filesystem files
cd /mnt/minfs
ls -la

#unmount filesystem
cd ..
umount /mnt/minfs

#unload module
rmmod minfs
