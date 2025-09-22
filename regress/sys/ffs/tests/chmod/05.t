#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/chmod/05.t,v 1.1 2007/01/17 01:42:08 pjd Exp $

desc="chmod returns EACCES when search permission is denied for a component of the path prefix"

n0=`namegen`
n1=`namegen`
n2=`namegen`

expect 0 mkdir ${n0} 0755
cdir=`pwd`
cd ${n0}
expect 0 mkdir ${n1} 0755
expect 0 chown ${n1} 65534 65534
expect 0 -u 65534 -g 65534 create ${n1}/${n2} 0644
expect 0 -u 65534 -g 65534 chmod ${n1}/${n2} 0642
expect 0642 -u 65534 -g 65534 stat ${n1}/${n2} mode
expect 0 chmod ${n1} 0644
expect EACCES -u 65534 -g 65534 chmod ${n1}/${n2} 0620
expect 0 chmod ${n1} 0755
expect 0 -u 65534 -g 65534 chmod ${n1}/${n2} 0420
expect 0420 -u 65534 -g 65534 stat ${n1}/${n2} mode
expect 0 -u 65534 -g 65534 unlink ${n1}/${n2}
expect 0 rmdir ${n1}
cd ${cdir}
expect 0 rmdir ${n0}
