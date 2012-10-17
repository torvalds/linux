#!/bin/bash
set -e

FILES=(
# arch
arch/arm/mach-rk2928/clock_data.c
arch/arm/mach-rk2928/ddr.c
)

DIRS=(
)

EXCLUDES=(
pack-kernel*

arch/arm/plat-rk/vpu*.c

arch/arm/mach-rk30/*.c
arch/arm/mach-rk30/*.h
arch/arm/mach-rk30/*.S
arch/arm/mach-rk30/*.inc
arch/arm/mach-rk30/Makefile*
arch/arm/mach-rk30/include
arch/arm/configs/rk30*

arch/arm/mach-rk29/*.c
arch/arm/mach-rk29/*.h
arch/arm/mach-rk29/*.S
arch/arm/mach-rk29/Makefile*
arch/arm/mach-rk29/include
arch/arm/configs/rk29_*

drivers/*rk28*.c
include/*rk28*

drivers/staging/rk29/vivante
drivers/staging/rk29/ipp/rk29-ipp.c

drivers/video/rockchip/lcdc/rk30*
drivers/video/rockchip/hdmi/chips/rk30/rk30*
drivers/video/rockchip/hdmi/chips/rk30/hdcp/rk30*

arch/arm/mach-rk2928/*fpga*
arch/arm/configs/*fpga*

arch/arm/mach-rk2928/board-rk2928-a720*
arch/arm/configs/rk2928_a720_defconfig

arch/arm/mach-rk2928/board-rk2928-tb*
arch/arm/configs/rk2928_tb_defconfig

arch/arm/mach-rk2928/board-rk2928.c
arch/arm/configs/rk2928_defconfig

arch/arm/mach-rk2928/board-rk2928-phonepad*
arch/arm/configs/rk2928_phonepad_defconfig
)

defconfig=${2-rk2928_sdk_defconfig}
. pack-kernel-common.sh
