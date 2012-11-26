#!/bin/bash
set -e

# ---------------------------------------------------------------------------
usage() {
	echo "usage: $0 [rk2928|rk30|rk3066b]"
	echo "example: $0 rk3066b"
	exit
}

while getopts "h" options; do
  case $options in
    h ) usage;;
  esac
done
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
kerndir=$(cd .; pwd)	# get absolute path
[ -d $kerndir ] || usage

arch=${1}
FILES=()
DIRS=()
EXCLUDES=()

if [ "$arch" == "rk30" ]; then

FILES=(
arch/arm/mach-rk30/clock_data.c
arch/arm/mach-rk30/ddr.c
)

EXCLUDES=(
arch/arm/mach-rk30/*rk3066b*
arch/arm/mach-rk30/include/mach/*rk3066b*

arch/arm/mach-rk29*/*.c
arch/arm/mach-rk29*/*.h
arch/arm/mach-rk29*/*.S
arch/arm/mach-rk29*/Makefile*
arch/arm/mach-rk29*/include
arch/arm/configs/rk29*

arch/arm/mach-rk30/board-rk30-phone-*
arch/arm/configs/rk30_phone_*

arch/arm/configs/rk30_phonepad_c8003*
)

defconfig=rk3066_sdk_defconfig

elif [ "$arch" == "rk2928" ]; then

FILES=(
arch/arm/mach-rk2928/clock_data.c
arch/arm/mach-rk2928/ddr.c
)

EXCLUDES=(
arch/arm/mach-rk30/*.c
arch/arm/mach-rk30/*.h
arch/arm/mach-rk30/*.S
arch/arm/mach-rk30/*.inc
arch/arm/mach-rk30/Makefile*
arch/arm/mach-rk30/include
arch/arm/configs/rk30*

drivers/video/rockchip/lcdc/rk30*
drivers/video/rockchip/hdmi/chips/rk30/rk30*
drivers/video/rockchip/hdmi/chips/rk30/hdcp/rk30*

arch/arm/mach-rk2928/board-rk2928-a720*
arch/arm/configs/rk2928_a720_defconfig

arch/arm/mach-rk2928/board-rk2928.c
arch/arm/configs/rk2928_defconfig

arch/arm/mach-rk2928/board-rk2928-phonepad*
arch/arm/configs/rk2928_phonepad_defconfig
)

defconfig=rk2928_sdk_defconfig

elif [ "$arch" == "rk3066b" ]; then

FILES=(
arch/arm/mach-rk30/clock_data-rk3066b.c
arch/arm/mach-rk30/ddr.c
)

EXCLUDES=(
arch/arm/mach-rk30/clock_data.c
arch/arm/mach-rk30/board-rk30-*
arch/arm/configs/rk30_*
arch/arm/configs/rk3066_*

arch/arm/mach-rk29*/*.c
arch/arm/mach-rk29*/*.h
arch/arm/mach-rk29*/*.S
arch/arm/mach-rk29*/Makefile*
arch/arm/mach-rk29*/include
arch/arm/configs/rk29*
)

defconfig=rk3066b_sdk_defconfig

else
	echo "unknown arch" && usage
fi

[ -f $kerndir/arch/arm/configs/$defconfig ] || usage

COMMON_EXCLUDES=(
pack-kernel*
arch/arm/plat-rk/vpu*.c
drivers/staging/rk29/vivante
drivers/staging/rk29/ipp/rk29-ipp.c
drivers/*rk28*.c
include/*rk28*
arch/arm/mach-rk29/*.c
arch/arm/mach-rk29/*.h
arch/arm/mach-rk29/*.S
arch/arm/mach-rk29/Makefile*
arch/arm/mach-rk29/include
arch/arm/mach-rk*/*-fpga*
arch/arm/mach-rk*/*-tb*
arch/arm/configs/rk29_*
arch/arm/configs/*_fpga_*
arch/arm/configs/*_tb_*
)
# ---------------------------------------------------------------------------

# make .uu
pushd $kerndir >/dev/null

declare -a files

for file in ${FILES[@]}; do
	[ -e ${file} ] && files=( ${files[@]} ${file} ) || echo No such file: ${file}
	[ -f ${file/.[cS]/.uu} ] && rm -f ${file/.[cS]/.uu}
done

for dir in ${DIRS[@]}; do
	[ -d $dir ] && find $dir -type f -name '*.uu' -print0 | xargs -0 rm -f
done

echo build kernel on $kerndir with $defconfig, arch is $arch
make clean >/dev/null 2>&1
make $defconfig >/dev/null 2>&1

make -j`grep 'processor' /proc/cpuinfo | wc -l` ${files[@]/.[cS]/.o} ${DIRS[@]}

for file in ${FILES[@]}; do
	filename=${file##*/} 
	base=${filename%%.*}
	dir=${file%/*}
	[ -f $dir/$base.o ] && echo UU $dir/$base.uu && uuencode $dir/$base.o $base.o > $dir/$base.uu
done

for d in ${DIRS[@]}; do
	for file in `find $d -type f -name '*.o'`; do
		filename=${file##*/} 
		base=${filename%%.*}
		dir=${file%/*}
		echo UU $dir/$base.uu && uuencode $dir/$base.o $base.o > $dir/$base.uu
	done
done

make distclean >/dev/null 2>&1

popd >/dev/null

# fix local version
echo "+" > $kerndir/.scmversion

# tar kernel
pushd $kerndir/../ >/dev/null
package=$(basename $kerndir).tar
ex=$package.ex
> $ex
for file in ${FILES[@]}; do
	echo "$file" >> $ex
done
for file in ${EXCLUDES[@]}; do
	echo "$file" >> $ex
done
for file in ${COMMON_EXCLUDES[@]}; do
	echo "$file" >> $ex
done
echo TAR $(pwd)/$package
tar cf $package --numeric-owner --exclude-from $ex --exclude=.git $(basename $kerndir)
#tar rf $package --numeric-owner --exclude=.git prebuilt/linux-x86/toolchain/arm-eabi-4.4.0
echo GZIP $(pwd)/$package.gz
gzip -9 -c $package > $package.gz
rm $ex
popd >/dev/null

rm -f $kerndir/.scmversion

echo done
