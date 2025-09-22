#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/open/09.t,v 1.1 2007/01/17 01:42:10 pjd Exp $

desc="O_CREAT is specified, the file does not exist, and the directory in which it is to be created has its immutable flag set"

n0=`namegen`
n1=`namegen`

if [ ${CHFLAGS} == "no" ]; then
	expect 0 mkdir ${n0} 0755
	expect EOPNOTSUPP chflags ${n0} SF_IMMUTABLE
	expect 0 rmdir ${n0}
	return 0
fi

expect 0 mkdir ${n0} 0755

expect 0 open ${n0}/${n1} O_RDONLY,O_CREAT 0644
expect 0 unlink ${n0}/${n1}

expect 0 chflags ${n0} SF_IMMUTABLE
expect EPERM open ${n0}/${n1} O_RDONLY,O_CREAT 0644
expect 0 chflags ${n0} none
expect 0 open ${n0}/${n1} O_RDONLY,O_CREAT 0644
expect 0 unlink ${n0}/${n1}

expect 0 chflags ${n0} UF_IMMUTABLE
expect EPERM open ${n0}/${n1} O_RDONLY,O_CREAT 0644
expect 0 chflags ${n0} none
expect 0 open ${n0}/${n1} O_RDONLY,O_CREAT 0644
expect 0 unlink ${n0}/${n1}

expect 0 chflags ${n0} SF_APPEND
expect 0 open ${n0}/${n1} O_RDONLY,O_CREAT 0644
expect 0 chflags ${n0} none
expect 0 unlink ${n0}/${n1}

expect 0 chflags ${n0} UF_APPEND
expect 0 open ${n0}/${n1} O_RDONLY,O_CREAT 0644
expect 0 chflags ${n0} none
expect 0 unlink ${n0}/${n1}

expect 0 rmdir ${n0}
