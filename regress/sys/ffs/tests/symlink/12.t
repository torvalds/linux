#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/symlink/12.t,v 1.1 2007/01/17 01:42:11 pjd Exp $

desc="symlink returns EFAULT if one of the pathnames specified is outside the process's allocated address space"

n0=`namegen`

expect EFAULT symlink NULL ${n0}
expect EFAULT symlink DEADCODE ${n0}
expect EFAULT symlink test NULL
expect EFAULT symlink test DEADCODE
expect EFAULT symlink NULL DEADCODE
expect EFAULT symlink DEADCODE NULL
