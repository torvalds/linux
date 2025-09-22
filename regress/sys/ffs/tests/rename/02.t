#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/rename/02.t,v 1.1 2007/01/17 01:42:10 pjd Exp $

desc="rename returns ENAMETOOLONG if an entire length of either path name exceeded 1023 characters"

n0=`namegen`

expect 0 mkdir ${name255} 0755
expect 0 mkdir ${name255}/${name255} 0755
expect 0 mkdir ${name255}/${name255}/${name255} 0755
expect 0 mkdir ${path1021} 0755
expect 0 create ${n0} 0644
expect 0 rename ${n0} ${path1023}
expect 0 rename ${path1023} ${n0}
expect ENAMETOOLONG rename ${n0} ${path1024}
expect 0 unlink ${n0}
expect ENAMETOOLONG rename ${path1024} ${n0}
expect 0 rmdir ${path1021}
expect 0 rmdir ${name255}/${name255}/${name255}
expect 0 rmdir ${name255}/${name255}
expect 0 rmdir ${name255}
