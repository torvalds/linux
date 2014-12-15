#! /bin/bash

#make UIMAGE_COMPRESSION=none uImage -j
#make modules
make uImage -j

make meson8b_skt.dtd
make meson8b_skt.dtb

#cd ../root/g18
#find .| cpio -o -H newc | gzip -9 > ../ramdisk.img

#rootfs.cpio -- original buildroot rootfs, busybox
#m8rootfs.cpio -- build from buildroot
ROOTFS="rootfs.cpio"

#cd ..
./mkbootimg --kernel ./arch/arm/boot/uImage --ramdisk ./${ROOTFS} --second ./arch/arm/boot/dts/amlogic/meson8b_skt.dtb --output ./m8boot.img
ls -l ./m8boot.img
echo "m8boot.img for m8 baby done"
