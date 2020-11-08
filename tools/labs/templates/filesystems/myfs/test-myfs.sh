#!/bin/sh

set -ex

#load module
insmod myfs.ko

#mount filesystem
mkdir -p /mnt/myfs
mount -t myfs none /mnt/myfs

#show registered filesystems
cat /proc/filesystems | grep myfs

#show mounted filesystems
cat /proc/mounts | grep myfs

#show filesystem statistics
stat -f /mnt/myfs

#list all filesystem files
cd /mnt/myfs
ls -la

#unmount filesystem
cd ..
umount /mnt/myfs

#unload module
rmmod myfs
