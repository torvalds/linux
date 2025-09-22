#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/open/12.t,v 1.1 2007/01/17 01:42:10 pjd Exp $

desc="open returns ELOOP if too many symbolic links were encountered in translating the pathname"

n0=`namegen`
n1=`namegen`

expect 0 symlink ${n0} ${n1}
expect 0 symlink ${n1} ${n0}
expect ELOOP open ${n0}/test O_RDONLY
expect ELOOP open ${n1}/test O_RDONLY
expect 0 unlink ${n0}
expect 0 unlink ${n1}
