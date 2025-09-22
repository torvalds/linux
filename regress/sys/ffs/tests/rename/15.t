#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/rename/15.t,v 1.1 2007/01/17 01:42:10 pjd Exp $

desc="rename returns EXDEV if the link named by 'to' and the file named by 'from' are on different file systems"

n0=`namegen`
n1=`namegen`
n2=`namegen`

expect 0 mkdir ${n0} 0755
dd if=/dev/zero of=tmpdisk bs=1k count=1024 status=none
vnconfig vnd1 tmpdisk
newfs /dev/rvnd1c >/dev/null
mount /dev/vnd1c ${n0}

expect 0 create ${n0}/${n1} 0644
expect EXDEV rename ${n0}/${n1} ${n2}
expect 0 unlink ${n0}/${n1}

expect 0 mkdir ${n0}/${n1} 0755
expect EXDEV rename ${n0}/${n1} ${n2}
expect 0 rmdir ${n0}/${n1}

expect 0 mkfifo ${n0}/${n1} 0644
expect EXDEV rename ${n0}/${n1} ${n2}
expect 0 unlink ${n0}/${n1}

expect 0 symlink test ${n0}/${n1}
expect EXDEV rename ${n0}/${n1} ${n2}
expect 0 unlink ${n0}/${n1}

umount /dev/vnd1c
vnconfig -u vnd1
rm tmpdisk
expect 0 rmdir ${n0}
