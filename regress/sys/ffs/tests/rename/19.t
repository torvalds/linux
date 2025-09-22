#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/rename/19.t,v 1.1 2007/01/17 01:42:10 pjd Exp $

desc="rename returns EINVAL when an attempt is made to rename '.' or '..'"

n0=`namegen`
n1=`namegen`
n2=`namegen`

expect 0 mkdir ${n0} 0755
expect 0 mkdir ${n0}/${n1} 0755

expect EINVAL rename ${n0}/${n1}/. ${n2}
expect EINVAL rename ${n0}/${n1}/.. ${n2}

expect 0 rmdir ${n0}/${n1}
expect 0 rmdir ${n0}
