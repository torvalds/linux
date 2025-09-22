#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/symlink/08.t,v 1.1 2007/01/17 01:42:11 pjd Exp $

desc="symlink returns EEXIST if the name2 argument already exists"

n0=`namegen`

expect 0 create ${n0} 0644
expect EEXIST symlink test ${n0}
expect 0 unlink ${n0}

expect 0 mkdir ${n0} 0755
expect EEXIST symlink test ${n0}
expect 0 rmdir ${n0}

expect 0 symlink test ${n0}
expect EEXIST symlink test ${n0}
expect 0 unlink ${n0}
