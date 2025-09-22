#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/link/04.t,v 1.1 2007/01/17 01:42:09 pjd Exp $

desc="link returns ENOENT if a component of either path prefix does not exist"

n0=`namegen`
n1=`namegen`
n2=`namegen`

expect 0 mkdir ${n0} 0755
expect ENOENT link ${n0}/${n1}/test ${n2}
expect 0 create ${n2} 0644
expect ENOENT link ${n2} ${n0}/${n1}/test
expect 0 unlink ${n2}
expect 0 rmdir ${n0}
