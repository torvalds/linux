#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/symlink/09.t,v 1.1 2007/01/17 01:42:11 pjd Exp $

desc="symlink returns EPERM if the parent directory of the file named by name2 has its immutable flag set"

n0=`namegen`
n1=`namegen`

expect 0 mkdir ${n0} 0755

if [ ${CHFLAGS} == "no" ]; then
	expect EOPNOTSUPP chflags ${n0} SF_IMMUTABLE
	expect 0 rmdir ${n0}
	return 0
fi

expect 0 symlink test ${n0}/${n1}
expect 0 unlink ${n0}/${n1}

expect 0 chflags ${n0} SF_IMMUTABLE
expect EPERM symlink test ${n0}/${n1}
expect 0 chflags ${n0} none
expect 0 symlink test ${n0}/${n1}
expect 0 unlink ${n0}/${n1}

expect 0 chflags ${n0} UF_IMMUTABLE
expect EPERM symlink test ${n0}/${n1}
expect 0 chflags ${n0} none
expect 0 symlink test ${n0}/${n1}
expect 0 unlink ${n0}/${n1}

expect 0 chflags ${n0} SF_APPEND
expect 0 symlink test ${n0}/${n1}
expect 0 chflags ${n0} none
expect 0 unlink ${n0}/${n1}

expect 0 chflags ${n0} UF_APPEND
expect 0 symlink test ${n0}/${n1}
expect 0 chflags ${n0} none
expect 0 unlink ${n0}/${n1}

expect 0 rmdir ${n0}
