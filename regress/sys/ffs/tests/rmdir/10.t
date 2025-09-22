#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/rmdir/10.t,v 1.1 2007/01/17 01:42:11 pjd Exp $

desc="rmdir returns EPERM if the parent directory of the named file has its immutable or append-only flag set"

n0=`namegen`
n1=`namegen`

if [ ${CHFLAGS} == "no" ]; then
	expect 0 mkdir ${n0} 0755
	expect EOPNOTSUPP chflags ${n0} SF_IMMUTABLE
	expect 0 rmdir ${n0}
	return 0
fi

expect 0 mkdir ${n0} 0755

expect 0 mkdir ${n0}/${n1} 0755
expect 0 chflags ${n0} SF_IMMUTABLE
expect EPERM rmdir ${n0}/${n1}
expect 0 chflags ${n0} none
expect 0 rmdir ${n0}/${n1}

expect 0 mkdir ${n0}/${n1} 0755
expect 0 chflags ${n0} UF_IMMUTABLE
expect EPERM rmdir ${n0}/${n1}
expect 0 chflags ${n0} none
expect 0 rmdir ${n0}/${n1}

expect 0 mkdir ${n0}/${n1} 0755
expect 0 chflags ${n0} SF_APPEND
expect EPERM rmdir ${n0}/${n1}
expect 0 chflags ${n0} none
expect 0 rmdir ${n0}/${n1}

expect 0 mkdir ${n0}/${n1} 0755
expect 0 chflags ${n0} UF_APPEND
expect EPERM rmdir ${n0}/${n1}
expect 0 chflags ${n0} none
expect 0 rmdir ${n0}/${n1}

expect 0 rmdir ${n0}
