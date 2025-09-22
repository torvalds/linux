#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/link/12.t,v 1.1 2007/01/17 01:42:09 pjd Exp $

desc="link returns EPERM if the source file has its immutable or append-only flag set"

n0=`namegen`
n1=`namegen`

if [ ${CHFLAGS} == "no" ]; then
	expect 0 create ${n0} 0644
	expect EOPNOTSUPP chflags ${n0} SF_IMMUTABLE
	expect 0 unlink ${n0}
	return 0
fi

expect 0 create ${n0} 0644

expect 0 link ${n0} ${n1}
expect 0 unlink ${n1}

expect 0 chflags ${n0} SF_IMMUTABLE
expect EPERM link ${n0} ${n1}
expect 0 chflags ${n0} none
expect 0 link ${n0} ${n1}
expect 0 unlink ${n1}

expect 0 chflags ${n0} UF_IMMUTABLE
expect EPERM link ${n0} ${n1}
expect 0 chflags ${n0} none
expect 0 link ${n0} ${n1}
expect 0 unlink ${n1}

expect 0 chflags ${n0} SF_APPEND
expect EPERM link ${n0} ${n1}
expect 0 chflags ${n0} none
expect 0 link ${n0} ${n1}
expect 0 unlink ${n1}

expect 0 chflags ${n0} UF_APPEND
expect EPERM link ${n0} ${n1}
expect 0 chflags ${n0} none
expect 0 link ${n0} ${n1}
expect 0 unlink ${n1}

expect 0 unlink ${n0}
