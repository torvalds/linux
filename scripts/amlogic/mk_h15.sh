#! /bin/bash

#make meson6tvc_h15_defconfig
#make UIMAGE_COMPRESSION=none uImage -j
make uImage -j12
#make modules

make meson6tvc_h15.dtd
make meson6tvc_h15.dtb

#cd ../root/g18
#find .| cpio -o -H newc | gzip -9 > ../ramdisk.img

#rootfs.cpio -- original buildroot rootfs, busybox
#m8rootfs.cpio -- build from buildroot
ROOTFS="rootfs.cpio"

#cd ..
./mkbootimg --kernel ./arch/arm/boot/uImage --ramdisk ./${ROOTFS} --second ./arch/arm/boot/dts/amlogic/meson6tvc_h15.dtb --output ./m6tvcboot.img
ls -l ./m6tvcboot.img
echo "m6tvcboot.img for h15 is done"
