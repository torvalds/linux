#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/rmdir/01.t,v 1.1 2007/01/17 01:42:11 pjd Exp $

desc="rmdir returns ENOTDIR if a component of the path is not a directory"

n0=`namegen`
n1=`namegen`

expect 0 mkdir ${n0} 0755
expect 0 create ${n0}/${n1} 0644
expect ENOTDIR rmdir ${n0}/${n1}/test
expect 0 unlink ${n0}/${n1}
expect 0 rmdir ${n0}

expect 0 create ${n0} 0644
expect ENOTDIR rmdir ${n0}
expect 0 unlink ${n0}

expect 0 symlink ${n1} ${n0}
expect ENOTDIR rmdir ${n0}
expect 0 unlink ${n0}

expect 0 mkfifo ${n0} 0644
expect ENOTDIR rmdir ${n0}
expect 0 unlink ${n0}
