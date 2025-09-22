#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/rmdir/04.t,v 1.1 2007/01/17 01:42:11 pjd Exp $

desc="rmdir returns ENOENT if the named directory does not exist"

n0=`namegen`
n1=`namegen`

expect 0 mkdir ${n0} 0755
expect 0 rmdir ${n0}
expect ENOENT rmdir ${n0}
expect ENOENT rmdir ${n1}
