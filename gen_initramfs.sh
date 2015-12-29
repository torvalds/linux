#!/bin/sh
cd initramfs
find . -print0 | cpio --null -ov --format=newc > ../rootfs.cpio
bzip2 -f ../rootfs.cpio

