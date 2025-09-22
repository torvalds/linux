#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/symlink/10.t,v 1.1 2007/01/17 01:42:11 pjd Exp $

desc="symlink returns EROFS if the file name2 would reside on a read-only file system"

n0=`namegen`
n1=`namegen`
n2=`namegen`

expect 0 mkdir ${n0} 0755
dd if=/dev/zero of=tmpdisk bs=1k count=1024 status=none
vnconfig vnd1 tmpdisk
newfs /dev/rvnd1c >/dev/null
mount /dev/vnd1c ${n0}

expect 0 symlink test ${n0}/${n1}
expect 0 unlink ${n0}/${n1}
mount -ur /dev/vnd1c
expect EROFS symlink test ${n0}/${n1}
mount -uw /dev/vnd1c
expect 0 symlink test ${n0}/${n1}
expect 0 unlink ${n0}/${n1}

umount /dev/vnd1c
vnconfig -u vnd1
rm tmpdisk
expect 0 rmdir ${n0}
