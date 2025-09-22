#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/chown/06.t,v 1.1 2007/01/17 01:42:08 pjd Exp $

desc="chown returns ELOOP if too many symbolic links were encountered in translating the pathname"

n0=`namegen`
n1=`namegen`

expect 0 symlink ${n0} ${n1}
expect 0 symlink ${n1} ${n0}
expect ELOOP chown ${n0}/test 65534 65534
expect ELOOP chown ${n1}/test 65534 65534
expect 0 unlink ${n0}
expect 0 unlink ${n1}
