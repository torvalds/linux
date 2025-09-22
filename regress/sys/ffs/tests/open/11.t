#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/open/11.t,v 1.1 2007/01/17 01:42:10 pjd Exp $

desc="open returns EPERM when the named file has its append-only flag set, the file is to be modified, and O_TRUNC is specified or O_APPEND is not specified"

n0=`namegen`

if [ ${CHFLAGS} == "no" ]; then
	expect 0 create ${n0} 0644
	expect EOPNOTSUPP chflags ${n0} SF_IMMUTABLE
	expect 0 unlink ${n0}
	return 0
fi

expect 0 create ${n0} 0644
expect 0 chflags ${n0} SF_APPEND
expect 0 open ${n0} O_WRONLY,O_APPEND
expect 0 open ${n0} O_RDWR,O_APPEND
expect EPERM open ${n0} O_WRONLY
expect EPERM open ${n0} O_RDWR
expect EPERM open ${n0} O_WRONLY,O_APPEND,O_TRUNC
expect EPERM open ${n0} O_RDWR,O_APPEND,O_TRUNC
expect 0 chflags ${n0} none
expect 0 unlink ${n0}

expect 0 create ${n0} 0644
expect 0 chflags ${n0} UF_APPEND
expect 0 open ${n0} O_WRONLY,O_APPEND
expect 0 open ${n0} O_RDWR,O_APPEND
expect EPERM open ${n0} O_WRONLY
expect EPERM open ${n0} O_RDWR
expect EPERM open ${n0} O_WRONLY,O_APPEND,O_TRUNC
expect EPERM open ${n0} O_RDWR,O_APPEND,O_TRUNC
expect 0 chflags ${n0} none
expect 0 unlink ${n0}
