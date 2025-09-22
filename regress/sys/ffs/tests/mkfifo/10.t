#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/mkfifo/10.t,v 1.1 2007/01/17 01:42:09 pjd Exp $

desc="mkfifo returns EPERM if the parent directory of the file to be created has its immutable flag set"

n0=`namegen`
n1=`namegen`

if [ ${CHFLAGS} == "no" ]; then
	expect 0 mkdir ${n0} 0755
	expect EOPNOTSUPP chflags ${n0} SF_IMMUTABLE
	expect 0 rmdir ${n0}
	return 0
fi

expect 0 mkdir ${n0} 0755

expect 0 mkfifo ${n0}/${n1} 0644
expect 0 unlink ${n0}/${n1}

expect 0 chflags ${n0} SF_IMMUTABLE
expect EPERM mkfifo ${n0}/${n1} 0644
expect 0 chflags ${n0} none
expect 0 mkfifo ${n0}/${n1} 0644
expect 0 unlink ${n0}/${n1}

expect 0 chflags ${n0} UF_IMMUTABLE
expect EPERM mkfifo ${n0}/${n1} 0644
expect 0 chflags ${n0} none
expect 0 mkfifo ${n0}/${n1} 0644
expect 0 unlink ${n0}/${n1}

expect 0 chflags ${n0} SF_APPEND
expect 0 mkfifo ${n0}/${n1} 0644
expect 0 chflags ${n0} none
expect 0 unlink ${n0}/${n1}

expect 0 chflags ${n0} UF_APPEND
expect 0 mkfifo ${n0}/${n1} 0644
expect 0 chflags ${n0} none
expect 0 unlink ${n0}/${n1}

expect 0 rmdir ${n0}
