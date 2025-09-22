#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/open/22.t,v 1.1 2007/01/17 01:42:10 pjd Exp $

desc="open returns EEXIST when O_CREAT and O_EXCL were specified and the file exists"

n0=`namegen`

expect 0 create ${n0} 0644
expect EEXIST open ${n0} O_CREAT,O_EXCL 0644
expect 0 unlink ${n0}

expect 0 mkdir ${n0} 0755
expect EEXIST open ${n0} O_CREAT,O_EXCL 0644
expect 0 rmdir ${n0}

expect 0 mkfifo ${n0} 0644
expect EEXIST open ${n0} O_CREAT,O_EXCL 0644
expect 0 unlink ${n0}

expect 0 symlink test ${n0}
expect EEXIST open ${n0} O_CREAT,O_EXCL 0644
expect 0 unlink ${n0}
