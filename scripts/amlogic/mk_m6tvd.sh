#! /bin/bash

#make meson6tvd_defconfig
#make UIMAGE_COMPRESSION=none uImage -j
make uImage -j12
#make modules

make meson6tvd_ref.dtd
make meson6tvd_ref.dtb

#cd ../root/g18
#find .| cpio -o -H newc | gzip -9 > ../ramdisk.img

#rootfs.cpio -- original buildroot rootfs, busybox
#m8rootfs.cpio -- build from buildroot
ROOTFS="rootfs.cpio"

#cd ..
./mkbootimg --kernel ./arch/arm/boot/uImage --ramdisk ./${ROOTFS} --second ./arch/arm/boot/dts/amlogic/meson6tvd_ref.dtb --output ./m6tvdboot.img
ls -l ./m6tvdboot.img
echo "m6tvdboot.img done"
