#!/bin/bash
set -e

# ---------------------------------------------------------------------------
usage() {
	echo usage: $0 kerneldir defconfig
	echo example: $0 . rk29_ddr3sdk_defconfig
	exit
}

while getopts "h" options; do
  case $options in
    h ) usage;;
  esac
done
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
kerndir=${1-.}
kerndir=$(cd $kerndir; pwd)	# get absolute path
[ -d $kerndir ] || usage

if [ "$defconfig" = ".config" ]; then
	[ -f $kerndir/.config ] || usage
else
	[ -f $kerndir/arch/arm/configs/$defconfig ] || usage
fi
# ---------------------------------------------------------------------------

# make .uu
pushd $kerndir >/dev/null

declare -a files

for file in ${FILES[@]}; do
	[ -e ${file} ] && files=( ${files[@]} ${file} ) || echo No such file: ${file}
	[ -f ${file/.[cS]/.uu} ] && rm -f ${file/.[cS]/.uu}
done

for d in ${DIRS[@]}; do
	[ -d $d ] && find $d -type f -name '*.uu' -print0 | xargs -0 rm -f
done

echo build kernel on $kerndir with $defconfig
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
echo TAR $(pwd)/$package
tar cf $package --numeric-owner --exclude-from $ex --exclude=.git $(basename $kerndir)
#tar rf $package --numeric-owner --exclude=.git prebuilt/linux-x86/toolchain/arm-eabi-4.4.0
echo GZIP $(pwd)/$package.gz
gzip -9 -c $package > $package.gz
rm $ex
popd >/dev/null

rm -f $kerndir/.scmversion

echo done


