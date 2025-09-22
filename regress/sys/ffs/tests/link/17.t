#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/link/17.t,v 1.1 2007/01/17 01:42:09 pjd Exp $

desc="link returns EFAULT if one of the pathnames specified is outside the process's allocated address space"

n0=`namegen`

expect 0 create ${n0} 0644
expect EFAULT link ${n0} NULL
expect EFAULT link ${n0} DEADCODE
expect 0 unlink ${n0}
expect EFAULT link NULL ${n0}
expect EFAULT link DEADCODE ${n0}
expect EFAULT link NULL DEADCODE
expect EFAULT link DEADCODE NULL
