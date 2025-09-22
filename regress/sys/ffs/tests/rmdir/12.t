#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/rmdir/12.t,v 1.1 2007/01/17 01:42:11 pjd Exp $

desc="rmdir returns EINVAL if the last component of the path is '.'"

n0=`namegen`
n1=`namegen`

expect 0 mkdir ${n0} 0755
expect 0 mkdir ${n0}/${n1} 0755
expect EINVAL rmdir ${n0}/${n1}/.

# POSIX: rmdir returns [EXIST] or [ENOTEMPTY] if the path argument names a
# directory that is not an empty directory, or there are hard links to the
# directory other than dot or a single entry in dot-dot.
expect "ENOTEMPTY|EEXIST" rmdir ${n0}/${n1}/..

expect 0 rmdir ${n0}/${n1}
expect 0 rmdir ${n0}
