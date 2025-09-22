#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/link/15.t,v 1.1 2007/01/17 01:42:09 pjd Exp $

desc="link returns ENOSPC if the directory in which the entry for the new link is being placed cannot be extended because there is no space left on the file system containing the directory"

n0=`namegen`
n1=`namegen`
n2=`namegen`

expect 0 mkdir ${n0} 0755
dd if=/dev/zero of=tmpdisk bs=1k count=256 status=none
vnconfig vnd1 tmpdisk
newfs /dev/rvnd1c >/dev/null
mount /dev/vnd1c ${n0}
expect 0 create ${n0}/${n1} 0644
i=0
while :; do
	ln ${n0}/${n1} ${n0}/${i} 2>/dev/null
	if [ $? -ne 0 ]; then
		break
	fi
	i=$((i + 1))
done
expect ENOSPC link ${n0}/${n1} ${n0}/${n2}
umount /dev/vnd1c
vnconfig -u vnd1
rm tmpdisk
expect 0 rmdir ${n0}
