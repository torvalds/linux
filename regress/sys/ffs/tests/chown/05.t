#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/chown/05.t,v 1.1 2007/01/17 01:42:08 pjd Exp $

desc="chown returns EACCES when search permission is denied for a component of the path prefix"

n0=`namegen`
n1=`namegen`
n2=`namegen`

expect 0 mkdir ${n0} 0755
cdir=`pwd`
cd ${n0}
expect 0 mkdir ${n1} 0755
expect 0 chown ${n1} 65534 65534
expect 0 -u 65534 -g 65534 create ${n1}/${n2} 0644
expect 0 -u 65534 -g 65533,65534 chown ${n1}/${n2} -1 65533
expect 65534,65533 -u 65534 -g 65534 stat ${n1}/${n2} uid,gid
expect 0 chmod ${n1} 0644
expect EACCES -u 65534 -g 65533,65534 chown ${n1}/${n2} -1 65534
expect 0 chmod ${n1} 0755
expect 65534,65533 -u 65534 -g 65534 stat ${n1}/${n2} uid,gid
expect 0 -u 65534 -g 65533,65534 chown ${n1}/${n2} -1 65534
expect 65534,65534 -u 65534 -g 65534 stat ${n1}/${n2} uid,gid
expect 0 -u 65534 -g 65534 unlink ${n1}/${n2}
expect 0 rmdir ${n1}
cd ${cdir}
expect 0 rmdir ${n0}
