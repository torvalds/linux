#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/chmod/04.t,v 1.1 2007/01/17 01:42:08 pjd Exp $

desc="chmod returns ENOENT if the named file does not exist"

n0=`namegen`
n1=`namegen`

expect 0 mkdir ${n0} 0755
expect ENOENT chmod ${n0}/${n1}/test 0644
expect ENOENT chmod ${n0}/${n1} 0644
expect 0 rmdir ${n0}
