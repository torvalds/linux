#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/mkdir/10.t,v 1.1 2007/01/17 01:42:09 pjd Exp $

desc="mkdir returns EEXIST if the named file exists"

n0=`namegen`

expect 0 mkdir ${n0} 0755
expect EEXIST mkdir ${n0} 0755
expect 0 rmdir ${n0}

expect 0 create ${n0} 0644
expect EEXIST mkdir ${n0} 0755
expect 0 unlink ${n0}

expect 0 symlink test ${n0}
expect EEXIST mkdir ${n0} 0755
expect 0 unlink ${n0}

expect 0 mkfifo ${n0} 0644
expect EEXIST mkdir ${n0} 0755
expect 0 unlink ${n0}
