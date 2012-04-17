#!/bin/bash
set -e

FILES=(
# arch
arch/arm/mach-rk29/clock.c
arch/arm/mach-rk29/ddr.c
arch/arm/mach-rk29/verifyID.c
)

DIRS=(
)

EXCLUDES=(
pack-kernel*

arch/arm/mach-rk29/vpu*.c
arch/arm/plat-rk/vpu*.c

arch/arm/mach-rk30/*.c
arch/arm/mach-rk30/*.h
arch/arm/mach-rk30/*.S
arch/arm/mach-rk30/Makefile*
arch/arm/mach-rk30/include
arch/arm/configs/rk30*
sound/*rk30*.c
drivers/*rk30*.c
drivers/*rk30*.h

drivers/*rk28*.c
include/*rk28*

arch/arm/mach-rk29/ddr_reconfig.c

drivers/staging/rk29/vivante

drivers/staging/rk29/ipp/rk29-ipp.c

arch/arm/mach-rk29/board-rk29sdk.c
arch/arm/configs/rk29_sdk_defconfig
arch/arm/configs/rk29_sdk_yaffs2_defconfig

arch/arm/mach-rk29/board-malata.c
arch/arm/mach-rk29/board-rk29malata-key.c
arch/arm/configs/rk29_malata_defconfig

arch/arm/mach-rk29/board-rk29-winaccord.c
arch/arm/configs/rk29_Winaccord_defconfig

arch/arm/mach-rk29/board-rk29-a22*
arch/arm/configs/rk29_a22_defconfig

arch/arm/mach-rk29/board-rk29-fih*
arch/arm/configs/rk29_FIH_defconfig

arch/arm/mach-rk29/board-rk29-k97*
arch/arm/mach-rk29/board-rk29k97*
arch/arm/configs/rk29_k97_defconfig

arch/arm/mach-rk29/board-rk29-newton*
arch/arm/mach-rk29/board-newton*
arch/arm/configs/rk29_newton_defconfig

arch/arm/mach-rk29/board-rk29-p91*
arch/arm/configs/rk29_p91_defconfig

arch/arm/mach-rk29/board-rk29-phonesdk*
arch/arm/configs/rk29_phonesdk_defconfig

arch/arm/mach-rk29/board-rk29-td8801*
arch/arm/configs/rk29_td8801_v2_defconfig

arch/arm/mach-rk29/board-rk29-z5*
arch/arm/configs/rk29_z5_defconfig
)

defconfig=${2-rk29_ddr3sdk_defconfig}
. pack-kernel-common.sh
