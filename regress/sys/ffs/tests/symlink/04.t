#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/symlink/04.t,v 1.1 2007/01/17 01:42:11 pjd Exp $

desc="symlink returns ENOENT if a component of the name2 path prefix does not exist"

n0=`namegen`
n1=`namegen`

expect 0 mkdir ${n0} 0755
expect ENOENT symlink test ${n0}/${n1}/test
expect 0 rmdir ${n0}
