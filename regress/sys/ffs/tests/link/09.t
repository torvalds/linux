#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/link/09.t,v 1.1 2007/01/17 01:42:09 pjd Exp $

desc="link returns ENOENT if the source file does not exist"

n0=`namegen`
n1=`namegen`

expect 0 create ${n0} 0644
expect 0 link ${n0} ${n1}
expect 0 unlink ${n0}
expect 0 unlink ${n1}
expect ENOENT link ${n0} ${n1}
