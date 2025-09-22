#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/link/01.t,v 1.1 2007/01/17 01:42:09 pjd Exp $

desc="link returns ENOTDIR if a component of either path prefix is not a directory"

n0=`namegen`
n1=`namegen`
n2=`namegen`

expect 0 mkdir ${n0} 0755
expect 0 create ${n0}/${n1} 0644
expect ENOTDIR link ${n0}/${n1}/test ${n0}/${n2}
expect 0 create ${n0}/${n2} 0644
expect ENOTDIR link ${n0}/${n2} ${n0}/${n1}/test
expect 0 unlink ${n0}/${n1}
expect 0 unlink ${n0}/${n2}
expect 0 rmdir ${n0}
