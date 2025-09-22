#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/mkfifo/07.t,v 1.1 2007/01/17 01:42:09 pjd Exp $

desc="mkfifo returns ELOOP if too many symbolic links were encountered in translating the pathname"

n0=`namegen`
n1=`namegen`

expect 0 symlink ${n0} ${n1}
expect 0 symlink ${n1} ${n0}
expect ELOOP mkfifo ${n0}/test 0644
expect ELOOP mkfifo ${n1}/test 0644
expect 0 unlink ${n0}
expect 0 unlink ${n1}
