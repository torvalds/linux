#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/chmod/11.t,v 1.1 2007/01/17 01:42:08 pjd Exp $

desc="chmod returns EFTYPE if the effective user ID is not the super-user, the mode includes the sticky bit (S_ISVTX), and path does not refer to a directory"

n0=`namegen`
n1=`namegen`

expect 0 mkdir ${n0} 0755
cdir=`pwd`
cd ${n0}

expect 0 mkdir ${n1} 0755
expect 0 chmod ${n1} 01755
expect 01755 stat ${n1} mode
expect 0 rmdir ${n1}

expect 0 create ${n1} 0644
expect 0 chmod ${n1} 01644
expect 01644 stat ${n1} mode
expect 0 unlink ${n1}

expect 0 mkdir ${n1} 0755
expect 0 chown ${n1} 65534 65534
expect 0 -u 65534 -g 65534 chmod ${n1} 01755
expect 01755 stat ${n1} mode
expect 0 rmdir ${n1}

expect 0 create ${n1} 0644
expect 0 chown ${n1} 65534 65534
expect EFTYPE -u 65534 -g 65534 chmod ${n1} 01644
expect 0644 stat ${n1} mode
expect 0 unlink ${n1}

cd ${cdir}
expect 0 rmdir ${n0}
