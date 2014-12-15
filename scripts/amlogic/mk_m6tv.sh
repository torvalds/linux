#! /bin/bash

make meson6tv_defconfig
#make UIMAGE_COMPRESSION=none uImage -j
make uImage -j12
#make modules

make meson6tv_ref.dtd
make meson6tv_ref.dtb

#cd ../root/g18
#find .| cpio -o -H newc | gzip -9 > ../ramdisk.img

#rootfs.cpio -- original buildroot rootfs, busybox
#m8rootfs.cpio -- build from buildroot
ROOTFS="rootfs.cpio"

#cd ..
./mkbootimg --kernel ./arch/arm/boot/uImage --ramdisk ./${ROOTFS} --second ./arch/arm/boot/dts/amlogic/meson6tv_ref.dtb --output ./m6tvboot.img
ls -l ./m6tvboot.img
echo "m6tvboot.img done"
