#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/rename/16.t,v 1.1 2007/01/17 01:42:10 pjd Exp $

desc="rename returns EROFS if the requested link requires writing in a directory on a read-only file system"

n0=`namegen`
n1=`namegen`
n2=`namegen`

expect 0 mkdir ${n0} 0755
dd if=/dev/zero of=tmpdisk bs=1k count=1024 status=none
vnconfig vnd1 tmpdisk
newfs /dev/rvnd1c >/dev/null
mount /dev/vnd1c ${n0}
expect 0 create ${n0}/${n1} 0644
mount -ur /dev/vnd1c

expect EROFS rename ${n0}/${n1} ${n0}/${n2}
expect EROFS rename ${n0}/${n1} ${n2}
expect 0 create ${n2} 0644
expect EROFS rename ${n2} ${n0}/${n2}
expect 0 unlink ${n2}

umount /dev/vnd1c
vnconfig -u vnd1
rm tmpdisk
expect 0 rmdir ${n0}
