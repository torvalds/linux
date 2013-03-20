#!/bin/bash
set -e

kerndir=$(cd .; pwd)	# get absolute path
[ -d $kerndir ] || exit

COMMON_EXCLUDES=(
pack-kernel*
defconfig
kernel.img
arch/arm/plat-rk/vpu*.c
arch/arm/plat-rk/rk_pm_tests/*.c
arch/arm/plat-rk/rk_pm_tests/*.h
drivers/staging/rk29/vivante
drivers/staging/rk29/ipp/rk29-ipp.c
drivers/*rk28*.c
include/*rk28*
arch/arm/mach-rk29/*.c
arch/arm/mach-rk29/*.h
arch/arm/mach-rk29/*.S
arch/arm/mach-rk29/Makefile*
arch/arm/mach-rk29/include
arch/arm/mach-rk30/*rk3168m*
arch/arm/mach-rk*/*-fpga*
arch/arm/configs/rk29_*
arch/arm/configs/rk3168m_*
arch/arm/configs/*_fpga_*
arch/arm/configs/rk30_phone_*
arch/arm/configs/*_openwrt_*

arch/arm/mach-rk30/board-rk30-phone-*
arch/arm/mach-rk30/board-rk30-phonepad.c
arch/arm/mach-rk30/board-rk30-phonepad-key.c
arch/arm/configs/rk30_phonepad*

arch/arm/mach-rk2928/board-rk2928-a720*
arch/arm/configs/rk2928_a720_defconfig

arch/arm/mach-rk30/*rk3028*
arch/arm/configs/rk3028_*
)

# ---------------------------------------------------------------------------
make -j`grep 'processor' /proc/cpuinfo | wc -l` distclean >/dev/null 2>&1

# fix local version
echo "+" > $kerndir/.scmversion

# tar kernel
pushd $kerndir/../ >/dev/null
package=$(basename $kerndir).tar
ex=$package.ex
> $ex
for file in ${COMMON_EXCLUDES[@]}; do
	echo "$file" >> $ex
done
echo TAR $(pwd)/$package
tar cf $package --numeric-owner --exclude-from $ex --exclude=.git $(basename $kerndir)
echo GZIP $(pwd)/$package.gz
gzip -9 -c $package > $package.gz
rm $ex
popd >/dev/null

rm -f $kerndir/.scmversion

echo done
