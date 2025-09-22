#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/rmdir/00.t,v 1.1 2007/01/17 01:42:11 pjd Exp $

desc="rmdir removes directories"

n0=`namegen`
n1=`namegen`

expect 0 mkdir ${n0} 0755
expect dir lstat ${n0} type
expect 0 rmdir ${n0}
expect ENOENT lstat ${n0} type

expect 0 mkdir ${n0} 0755
expect 0 mkdir ${n0}/${n1} 0755
time=`${FSTEST} stat ${n0} ctime`
sleep 1
expect 0 rmdir ${n0}/${n1}
mtime=`${FSTEST} stat ${n0} mtime`
test_check $time -lt $mtime
ctime=`${FSTEST} stat ${n0} ctime`
test_check $time -lt $ctime
expect 0 rmdir ${n0}
