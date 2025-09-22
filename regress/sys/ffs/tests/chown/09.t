#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/chown/09.t,v 1.1 2007/01/17 01:42:08 pjd Exp $

desc="chown returns EROFS if the named file resides on a read-only file system"

n0=`namegen`
n1=`namegen`

expect 0 mkdir ${n0} 0755
dd if=/dev/zero of=tmpdisk bs=1k count=1024 status=none
vnconfig vnd1 tmpdisk
newfs /dev/rvnd1c >/dev/null
mount /dev/vnd1c ${n0}
expect 0 create ${n0}/${n1} 0644
expect 0 chown ${n0}/${n1} 65534 65534
expect 65534,65534 stat ${n0}/${n1} uid,gid
mount -ur /dev/vnd1c
expect EROFS chown ${n0}/${n1} 65533 65533
expect 65534,65534 stat ${n0}/${n1} uid,gid
mount -uw /dev/vnd1c
expect 0 chown ${n0}/${n1} 65533 65533
expect 65533,65533 stat ${n0}/${n1} uid,gid
expect 0 unlink ${n0}/${n1}
umount /dev/vnd1c
vnconfig -u vnd1
rm tmpdisk
expect 0 rmdir ${n0}
