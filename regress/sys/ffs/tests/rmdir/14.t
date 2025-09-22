#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/rmdir/14.t,v 1.1 2007/01/17 01:42:11 pjd Exp $

desc="rmdir returns EROFS if the named file resides on a read-only file system"

n0=`namegen`
n1=`namegen`

expect 0 mkdir ${n0} 0755
dd if=/dev/zero of=tmpdisk bs=1k count=1024 status=none
vnconfig vnd1 tmpdisk
newfs /dev/rvnd1c >/dev/null
mount /dev/vnd1c ${n0}
expect 0 mkdir ${n0}/${n1} 0755
mount -ur /dev/vnd1c
expect EROFS rmdir ${n0}/${n1}
mount -uw /dev/vnd1c
expect 0 rmdir ${n0}/${n1}
umount /dev/vnd1c
vnconfig -u vnd1
rm tmpdisk
expect 0 rmdir ${n0}
