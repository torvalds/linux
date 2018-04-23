#!/bin/sh

set -x

# load module
insmod myfs.ko

# mount filesystem
mkdir -p /mnt/myfs
mount -t myfs none /mnt/myfs
ls -laid /mnt/myfs

cd /mnt/myfs

# create directory
mkdir mydir
ls -la

# create subdirectory
cd mydir
mkdir mysubdir
ls -lai

# rename subdirectory
mv mysubdir myrenamedsubdir
ls -lai

# delete renamed subdirectory
rmdir myrenamedsubdir
ls -la

# create file
touch myfile
ls -lai

# rename file
mv myfile myrenamedfile
ls -lai

# delete renamed file
rm myrenamedfile

# delete directory
cd ..
rmdir mydir
ls -la

# unmount filesystem
cd ..
umount /mnt/myfs

# unload module
rmmod myfs
