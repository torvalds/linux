#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/open/23.t,v 1.1 2007/01/17 01:42:10 pjd Exp $

desc="open returns EINVAL when an attempt was made to open a descriptor with an illegal combination of O_RDONLY, O_WRONLY, and O_RDWR"

n0=`namegen`

expect 0 create ${n0} 0644
expect EINVAL open ${n0} O_WRONLY,O_RDWR
expect EINVAL open ${n0} O_RDONLY,O_WRONLY,O_RDWR

# POSIX: The value of the oflag argument is not valid.
expect EINVAL open ${n0} O_RDONLY,O_TRUNC

expect 0 unlink ${n0}
