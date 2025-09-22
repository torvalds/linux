#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/open/04.t,v 1.1 2007/01/17 01:42:10 pjd Exp $

desc="open returns ENOENT if a component of the path name that must exist does not exist or O_CREAT is not set and the named file does not exist"

n0=`namegen`
n1=`namegen`

expect 0 mkdir ${n0} 0755
expect ENOENT open ${n0}/${n1}/test O_CREAT 0644
expect ENOENT open ${n0}/${n1} O_RDONLY
expect 0 rmdir ${n0}
