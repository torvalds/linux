#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/chflags/09.t,v 1.1 2007/01/17 01:42:08 pjd Exp $

desc="chflags returns EPERM when one of SF_IMMUTABLE or SF_APPEND is set and securelevel is greater than 0"

n0=`namegen`
n1=`namegen`
n2=`namegen`

expect 0 mkdir ${n0} 0755
cdir=`pwd`
cd ${n0}

expect 0 create ${n1} 0644
expect 0 chown ${n1} 65534 65534
for flag in SF_IMMUTABLE SF_APPEND; do
	expect 0 chflags ${n1} ${flag}
	expect ${flag} stat ${n1} flags
done
expect 0 chflags ${n1} none
expect 0 unlink ${n1}

expect 0 mkdir ${n1} 0755
expect 0 chown ${n1} 65534 65534
for flag in SF_IMMUTABLE SF_APPEND; do
	expect 0 chflags ${n1} ${flag}
	expect ${flag} stat ${n1} flags
done
expect 0 chflags ${n1} none
expect 0 rmdir ${n1}

expect 0 mkfifo ${n1} 0644
expect 0 chown ${n1} 65534 65534
for flag in SF_IMMUTABLE SF_APPEND; do
	expect 0 chflags ${n1} ${flag}
	expect ${flag} stat ${n1} flags
done
expect 0 chflags ${n1} none
expect 0 unlink ${n1}

expect 0 symlink ${n2} ${n1}
expect 0 lchown ${n1} 65534 65534
for flag in SF_IMMUTABLE SF_APPEND; do
	expect 0 lchflags ${n1} ${flag}
	expect ${flag} lstat ${n1} flags
done
expect 0 lchflags ${n1} none
expect 0 unlink ${n1}

cd ${cdir}
expect 0 rmdir ${n0}
