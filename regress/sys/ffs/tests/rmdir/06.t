#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/rmdir/06.t,v 1.1 2007/01/17 01:42:11 pjd Exp $

desc="rmdir returns EEXIST or ENOTEMPTY the named directory contains files other than '.' and '..' in it"

n0=`namegen`
n1=`namegen`

expect 0 mkdir ${n0} 0755
expect 0 mkdir ${n0}/${n1} 0755
expect "EEXIST|ENOTEMPTY" rmdir ${n0}
expect 0 rmdir ${n0}/${n1}
expect 0 rmdir ${n0}

expect 0 mkdir ${n0} 0755
expect 0 create ${n0}/${n1} 0644
expect "EEXIST|ENOTEMPTY" rmdir ${n0}
expect 0 unlink ${n0}/${n1}
expect 0 rmdir ${n0}

expect 0 mkdir ${n0} 0755
expect 0 symlink test ${n0}/${n1}
expect "EEXIST|ENOTEMPTY" rmdir ${n0}
expect 0 unlink ${n0}/${n1}
expect 0 rmdir ${n0}

expect 0 mkdir ${n0} 0755
expect 0 mkfifo ${n0}/${n1} 0644
expect "EEXIST|ENOTEMPTY" rmdir ${n0}
expect 0 unlink ${n0}/${n1}
expect 0 rmdir ${n0}
