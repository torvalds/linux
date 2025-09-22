#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/unlink/07.t,v 1.1 2007/01/17 01:42:12 pjd Exp $

desc="unlink returns ELOOP if too many symbolic links were encountered in translating the pathname"

n0=`namegen`
n1=`namegen`

expect 0 symlink ${n0} ${n1}
expect 0 symlink ${n1} ${n0}
expect ELOOP unlink ${n0}/test
expect ELOOP unlink ${n1}/test
expect 0 unlink ${n0}
expect 0 unlink ${n1}
