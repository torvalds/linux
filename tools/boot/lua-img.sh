#!/bin/sh
# $FreeBSD$

# Quick script to build a suitable /boot dir somewhere in the tree for testing.
# dir may be passed in, will default to /tmp/loadertest if not specified

die() {
    echo $*
    exit 1
}

dir=$1
cd $(make -V SRCTOP)

[ -n "$dir" ] || dir=/tmp/loadertest

set -e

rm -rf ${dir}
mkdir -p ${dir}
mtree -deUW -f etc/mtree/BSD.root.dist -p ${dir}
mtree -deUW -f etc/mtree/BSD.usr.dist -p ${dir}/usr
cd stand
make all install DESTDIR=${dir} NO_ROOT=t MK_LOADER_LUA=yes MK_FORTH=no MK_INSTALL_AS_USER=yes
mkdir -p ${dir}/boot/kernel
cp /boot/kernel/kernel ${dir}/boot/kernel
