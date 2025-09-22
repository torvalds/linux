#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/mkdir/04.t,v 1.1 2007/01/17 01:42:09 pjd Exp $

desc="mkdir returns ENOENT if a component of the path prefix does not exist"

n0=`namegen`
n1=`namegen`

expect 0 mkdir ${n0} 0755
expect ENOENT mkdir ${n0}/${n1}/test 0755
expect 0 rmdir ${n0}
