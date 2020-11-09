#!/bin/sh

set -ex

# load module
insmod myfs.ko

# mount filesystem
mkdir -p /mnt/myfs
mount -t myfs none /mnt/myfs
ls -laid /mnt/myfs

cd /mnt/myfs

# create file
touch myfile
ls -lai

# rename file
mv myfile myrenamedfile
ls -lai

# create link to file
ln myrenamedfile mylink
ls -lai

# read/write file
echo "message" > myrenamedfile
cat myrenamedfile

# remove link to file
rm mylink
ls -la

# delete file
rm -f myrenamedfile
ls -la

# unmount filesystem
cd ..
umount /mnt/myfs

# unload module
rmmod myfs
