#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/symlink/06.t,v 1.1 2007/01/17 01:42:11 pjd Exp $

desc="symlink returns EACCES if the parent directory of the file to be created denies write permission"

n0=`namegen`
n1=`namegen`
n2=`namegen`

expect 0 mkdir ${n0} 0755
cdir=`pwd`
cd ${n0}

expect 0 mkdir ${n1} 0755
expect 0 chown ${n1} 65534 65534

expect 0 -u 65534 -g 65534 symlink test ${n1}/${n2}
expect 0 -u 65534 -g 65534 unlink ${n1}/${n2}

expect 0 chmod ${n1} 0555
expect EACCES -u 65534 -g 65534 symlink test ${n1}/${n2}
expect 0 chmod ${n1} 0755
expect 0 -u 65534 -g 65534 symlink test ${n1}/${n2}
expect 0 unlink ${n1}/${n2}

expect 0 rmdir ${n1}

cd ${cdir}
expect 0 rmdir ${n0}
