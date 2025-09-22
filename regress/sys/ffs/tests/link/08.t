#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/link/08.t,v 1.1 2007/01/17 01:42:09 pjd Exp $

desc="link returns ELOOP if too many symbolic links were encountered in translating one of the pathnames"

n0=`namegen`
n1=`namegen`
n2=`namegen`

expect 0 symlink ${n0} ${n1}
expect 0 symlink ${n1} ${n0}
expect ELOOP link ${n0}/test ${n2}
expect ELOOP link ${n1}/test ${n2}
expect 0 create ${n2} 0644
expect ELOOP link ${n2} ${n0}/test
expect ELOOP link ${n2} ${n1}/test
expect 0 unlink ${n0}
expect 0 unlink ${n1}
expect 0 unlink ${n2}
