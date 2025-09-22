#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/symlink/11.t,v 1.1 2007/01/17 01:42:11 pjd Exp $

desc="symlink returns ENOSPC if there are no free inodes on the file system on which the symbolic link is being created"

n0=`namegen`
n1=`namegen`

expect 0 mkdir ${n0} 0755
dd if=/dev/zero of=tmpdisk bs=1k count=256 status=none
vnconfig vnd1 tmpdisk
newfs /dev/rvnd1c >/dev/null
mount /dev/vnd1c ${n0}

i=0
while :; do
	ln -s test ${n0}/${i} 2>/dev/null
	if [ $? -ne 0 ]; then
		break
	fi
	i=$((i + 1))
done
expect ENOSPC symlink test ${n0}/${n1}
umount /dev/vnd1c
vnconfig -u vnd1
rm tmpdisk
expect 0 rmdir ${n0}
