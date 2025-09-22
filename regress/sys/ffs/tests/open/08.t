#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/open/08.t,v 1.1 2007/01/17 01:42:10 pjd Exp $

desc="open returns EACCES when O_CREAT is specified, the file does not exist, and the directory in which it is to be created does not permit writing"

n0=`namegen`
n1=`namegen`

expect 0 mkdir ${n0} 0755
cdir=`pwd`
cd ${n0}
expect EACCES -u 65534 -g 65534 open ${n1} O_RDONLY,O_CREAT 0644
cd ${cdir}
expect 0 rmdir ${n0}
