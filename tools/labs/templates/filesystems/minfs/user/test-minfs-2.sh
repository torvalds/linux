#!/bin/sh

pushd . > /dev/null 2>&1

# load module
insmod ../kernel/minfs.ko

# create mount point
mkdir -p /mnt/minfs

# format partition
./mkfs.minfs /dev/vdb

# mount filesystem
mount -t minfs /dev/vdb /mnt/minfs

# change to minfs root folder
cd /mnt/minfs

# create new file
touch b.txt && echo "OK. File created." || echo "NOT OK. File creation failed."

# unmount filesystem
cd ..
umount /mnt/minfs

popd > /dev/null 2>&1

# mount filesystem
mount -t minfs /dev/vdb /mnt/minfs

# check whether b.txt is still there
ls /mnt/minfs | grep b.txt && echo "OK. File b.txt exists " || echo "NOT OK. File b.txt does not exist."

# unmount filesystem
umount /mnt/minfs

# unload module
rmmod minfs
