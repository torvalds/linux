#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/rename/03.t,v 1.1 2007/01/17 01:42:10 pjd Exp $

desc="rename returns ENOENT if a component of the 'from' path does not exist, or a path prefix of 'to' does not exist"

n0=`namegen`
n1=`namegen`
n2=`namegen`

expect 0 mkdir ${n0} 0755
expect ENOENT rename ${n0}/${n1}/test ${n2}
expect 0 create ${n2} 0644
expect ENOENT rename ${n2} ${n0}/${n1}/test
expect 0 unlink ${n2}
expect 0 rmdir ${n0}
