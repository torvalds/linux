#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/unlink/04.t,v 1.1 2007/01/17 01:42:12 pjd Exp $

desc="unlink returns ENOENT if the named file does not exist"

n0=`namegen`
n1=`namegen`

expect 0 create ${n0} 0644
expect 0 unlink ${n0}
expect ENOENT unlink ${n0}
expect ENOENT unlink ${n1}
