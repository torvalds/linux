#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/truncate/13.t,v 1.1 2007/01/17 01:42:12 pjd Exp $

desc="truncate returns EINVAL if the length argument was less than 0"

n0=`namegen`

expect 0 create ${n0} 0644
expect EINVAL truncate ${n0} -1
expect EINVAL truncate ${n0} -999999
expect 0 unlink ${n0}
