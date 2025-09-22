#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/link/13.t,v 1.1 2007/01/17 01:42:09 pjd Exp $

desc="link returns EPERM if the parent directory of the destination file has its immutable flag set"

n0=`namegen`
n1=`namegen`
n2=`namegen`

if [ ${CHFLAGS} == "no" ]; then
	expect 0 mkdir ${n0} 0755
	expect EOPNOTSUPP chflags ${n0} SF_IMMUTABLE
	expect 0 rmdir ${n0}
	return 0
fi

expect 0 mkdir ${n0} 0755

expect 0 create ${n0}/${n1} 0644
expect 0 link ${n0}/${n1} ${n0}/${n2}
expect 0 unlink ${n0}/${n2}

expect 0 chflags ${n0} SF_IMMUTABLE
expect EPERM link ${n0}/${n1} ${n0}/${n2}
expect 0 chflags ${n0} none
expect 0 link ${n0}/${n1} ${n0}/${n2}
expect 0 unlink ${n0}/${n2}

expect 0 chflags ${n0} UF_IMMUTABLE
expect EPERM link ${n0}/${n1} ${n0}/${n2}
expect 0 chflags ${n0} none
expect 0 link ${n0}/${n1} ${n0}/${n2}
expect 0 unlink ${n0}/${n2}

expect 0 chflags ${n0} SF_APPEND
expect 0 link ${n0}/${n1} ${n0}/${n2}
expect 0 chflags ${n0} none
expect 0 unlink ${n0}/${n2}

expect 0 chflags ${n0} UF_APPEND
expect 0 link ${n0}/${n1} ${n0}/${n2}
expect 0 chflags ${n0} none
expect 0 unlink ${n0}/${n2}

expect 0 unlink ${n0}/${n1}
expect 0 rmdir ${n0}
