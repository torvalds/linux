#!/bin/bash

rm -f testfile.img
dd if=/dev/zero of=testfile.img bs=1M seek=1000 count=1
DEVICE=$(losetup --show -f testfile.img)
mkfs.btrfs -f $DEVICE
mkdir tmpmnt
./tracex7 $DEVICE
if [ $? -eq 0 ]
then
	echo "SUCCESS!"
else
	echo "FAILED!"
fi
losetup -d $DEVICE
