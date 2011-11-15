#!/bin/bash
set -e
#########################################################################
#
#          Simple build scripts to build krenel(with rootfs)  -- by Benn
#
#########################################################################


#Setup common variables
export ARCH=arm
export CROSS_COMPILE=arm-none-linux-gnueabi-
export AS=${CROSS_COMPILE}as
export LD=${CROSS_COMPILE}ld
export CC=${CROSS_COMPILE}gcc
export AR=${CROSS_COMPILE}ar
export NM=${CROSS_COMPILE}nm
export STRIP=${CROSS_COMPILE}strip
export OBJCOPY=${CROSS_COMPILE}objcopy
export OBJDUMP=${CROSS_COMPILE}objdump

KERNEL_VERSION="3.0"
LICHEE_KDIR=`pwd`
LICHEE_MOD_DIR==${LICHEE_KDIR}/output/lib/modules/${KERNEL_VERSION}

CONFIG_CHIP_ID=1123

update_kern_ver()
{
    if [ -r include/generated/utsrelease.h ]; then
        KERNEL_VERSION=`cat include/generated/utsrelease.h |awk -F\" '{print $2}'`
    fi
    LICHEE_MOD_DIR=${LICHEE_KDIR}/output/lib/modules/${KERNEL_VERSION}
}

show_help()
{
    printf "Build script for Lichee system\n"
    printf "  Invalid Option:\n"
    printf "  help      - show this help\n"
    printf "  kernel    - build kernel for sun4i\n"
    printf "  modules   - build external modules for sun4i\n"
    printf "  clean     - clean all\n"
    printf "\n"
}


build_kernel()
{
    if [ ! -e .config ]; then
	echo -e "\n\t\tUsing default config... ...!\n"
	cp arch/arm/configs/sun4i_defconfig .config

    fi

    make KALLSYMS_EXTRA_PASS=1 ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} -j8
    update_kern_ver

    if [ -d output ]; then
	rm -rf output
    fi
    mkdir -p $LICHEE_MOD_DIR

    ${OBJCOPY} -R .note.gnu.build-id -S -O binary vmlinux output/bImage
    cp -vf arch/arm/boot/[zu]Image output/
    cp .config output/


    for file in $(find drivers sound crypto block fs security net -name "*.ko"); do
	cp $file ${LICHEE_MOD_DIR}
    done
    cp -f Module.symvers modules.* ${LICHEE_MOD_DIR}
}

build_modules()
{
    echo "Building modules"
}

clean_kernel()
{
    make clean
    rm -rf output/*
}

clean_modules()
{
    echo "Cleaning modules"
}

#####################################################################
#
#                      Main Runtine
#
#####################################################################

LICHEE_ROOT=`(cd ${LICHEE_KDIR}/..; pwd)`
export PATH=${LICHEE_ROOT}/buildroot/output/external-toolchain/bin:$PATH

case "$1" in
kernel)
    build_kernel
    ;;
modules)
    build_modules
    ;;
clean)
    clean_kernel
    clean_modules
    ;;
all)
    build_kernel
    build_modules
    ;;
*)
    show_help
    ;;
esac

