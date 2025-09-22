#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/chflags/00.t,v 1.1 2007/01/17 01:42:08 pjd Exp $

desc="chflags changes flags"

n0=`namegen`
n1=`namegen`
n2=`namegen`

expect 0 mkdir ${n2} 0755
cdir=`pwd`
cd ${n2}

expect 0 create ${n0} 0644
expect none stat ${n0} flags
expect 0 chflags ${n0} UF_NODUMP,UF_IMMUTABLE,UF_APPEND,UF_OPAQUE,SF_ARCHIVED,SF_IMMUTABLE,SF_APPEND
expect UF_NODUMP,UF_IMMUTABLE,UF_APPEND,UF_OPAQUE,SF_ARCHIVED,SF_IMMUTABLE,SF_APPEND stat ${n0} flags
expect 0 chflags ${n0} UF_NODUMP,UF_IMMUTABLE,UF_APPEND,UF_OPAQUE
expect UF_NODUMP,UF_IMMUTABLE,UF_APPEND,UF_OPAQUE stat ${n0} flags
expect 0 chflags ${n0} SF_ARCHIVED,SF_IMMUTABLE,SF_APPEND
expect SF_ARCHIVED,SF_IMMUTABLE,SF_APPEND stat ${n0} flags
expect 0 chflags ${n0} none
expect none stat ${n0} flags
expect 0 unlink ${n0}

expect 0 mkdir ${n0} 0644
expect none stat ${n0} flags
expect 0 chflags ${n0} UF_NODUMP,UF_IMMUTABLE,UF_APPEND,UF_OPAQUE,SF_ARCHIVED,SF_IMMUTABLE,SF_APPEND
expect UF_NODUMP,UF_IMMUTABLE,UF_APPEND,UF_OPAQUE,SF_ARCHIVED,SF_IMMUTABLE,SF_APPEND stat ${n0} flags
expect 0 chflags ${n0} UF_NODUMP,UF_IMMUTABLE,UF_APPEND,UF_OPAQUE
expect UF_NODUMP,UF_IMMUTABLE,UF_APPEND,UF_OPAQUE stat ${n0} flags
expect 0 chflags ${n0} SF_ARCHIVED,SF_IMMUTABLE,SF_APPEND
expect SF_ARCHIVED,SF_IMMUTABLE,SF_APPEND stat ${n0} flags
expect 0 chflags ${n0} none
expect none stat ${n0} flags
expect 0 rmdir ${n0}

expect 0 mkfifo ${n0} 0644
expect none stat ${n0} flags
expect 0 chflags ${n0} UF_NODUMP,UF_IMMUTABLE,UF_APPEND,UF_OPAQUE,SF_ARCHIVED,SF_IMMUTABLE,SF_APPEND
expect UF_NODUMP,UF_IMMUTABLE,UF_APPEND,UF_OPAQUE,SF_ARCHIVED,SF_IMMUTABLE,SF_APPEND stat ${n0} flags
expect 0 chflags ${n0} UF_NODUMP,UF_IMMUTABLE,UF_APPEND,UF_OPAQUE
expect UF_NODUMP,UF_IMMUTABLE,UF_APPEND,UF_OPAQUE stat ${n0} flags
expect 0 chflags ${n0} SF_ARCHIVED,SF_IMMUTABLE,SF_APPEND
expect SF_ARCHIVED,SF_IMMUTABLE,SF_APPEND stat ${n0} flags
expect 0 chflags ${n0} none
expect none stat ${n0} flags
expect 0 unlink ${n0}

expect 0 create ${n0} 0644
expect 0 symlink ${n0} ${n1}
expect none stat ${n1} flags
expect none lstat ${n1} flags
expect 0 chflags ${n1} UF_NODUMP,UF_IMMUTABLE,UF_APPEND,UF_OPAQUE,SF_ARCHIVED,SF_IMMUTABLE,SF_APPEND
expect UF_NODUMP,UF_IMMUTABLE,UF_APPEND,UF_OPAQUE,SF_ARCHIVED,SF_IMMUTABLE,SF_APPEND stat ${n1} flags
expect none lstat ${n1} flags
expect 0 chflags ${n1} UF_NODUMP,UF_IMMUTABLE,UF_APPEND,UF_OPAQUE
expect UF_NODUMP,UF_IMMUTABLE,UF_APPEND,UF_OPAQUE stat ${n1} flags
expect none lstat ${n1} flags
expect 0 chflags ${n1} SF_ARCHIVED,SF_IMMUTABLE,SF_APPEND
expect SF_ARCHIVED,SF_IMMUTABLE,SF_APPEND stat ${n1} flags
expect none lstat ${n1} flags
expect 0 chflags ${n1} none
expect none stat ${n1} flags
expect none lstat ${n1} flags
expect 0 unlink ${n1}
expect 0 unlink ${n0}

expect 0 create ${n0} 0644
expect 0 symlink ${n0} ${n1}
expect none stat ${n1} flags
expect none lstat ${n1} flags
expect 0 lchflags ${n1} UF_NODUMP,UF_IMMUTABLE,UF_APPEND,UF_OPAQUE,SF_ARCHIVED,SF_IMMUTABLE,SF_APPEND
expect UF_NODUMP,UF_IMMUTABLE,UF_APPEND,UF_OPAQUE,SF_ARCHIVED,SF_IMMUTABLE,SF_APPEND lstat ${n1} flags
expect none stat ${n1} flags
expect 0 lchflags ${n1} UF_NODUMP,UF_IMMUTABLE,UF_APPEND,UF_OPAQUE
expect UF_NODUMP,UF_IMMUTABLE,UF_APPEND,UF_OPAQUE lstat ${n1} flags
expect none stat ${n1} flags
expect 0 lchflags ${n1} SF_ARCHIVED,SF_IMMUTABLE,SF_APPEND
expect SF_ARCHIVED,SF_IMMUTABLE,SF_APPEND lstat ${n1} flags
expect none stat ${n1} flags
expect 0 lchflags ${n1} none
expect none lstat ${n1} flags
expect none stat ${n1} flags
expect 0 unlink ${n1}
expect 0 unlink ${n0}

# successful chflags(2) updates ctime.
expect 0 create ${n0} 0644
for flag in UF_NODUMP UF_IMMUTABLE UF_APPEND UF_OPAQUE SF_ARCHIVED SF_IMMUTABLE SF_APPEND none; do
	ctime1=`${FSTEST} stat ${n0} ctime`
	sleep 1
	expect 0 chflags ${n0} ${flag}
	ctime2=`${FSTEST} stat ${n0} ctime`
	test_check $ctime1 -lt $ctime2
done
expect 0 unlink ${n0}

expect 0 mkdir ${n0} 0755
for flag in UF_NODUMP UF_IMMUTABLE UF_APPEND UF_OPAQUE SF_ARCHIVED SF_IMMUTABLE SF_APPEND none; do
	ctime1=`${FSTEST} stat ${n0} ctime`
	sleep 1
	expect 0 chflags ${n0} ${flag}
	ctime2=`${FSTEST} stat ${n0} ctime`
	test_check $ctime1 -lt $ctime2
done
expect 0 rmdir ${n0}

expect 0 mkfifo ${n0} 0644
for flag in UF_NODUMP UF_IMMUTABLE UF_APPEND UF_OPAQUE SF_ARCHIVED SF_IMMUTABLE SF_APPEND none; do
	ctime1=`${FSTEST} stat ${n0} ctime`
	sleep 1
	expect 0 chflags ${n0} ${flag}
	ctime2=`${FSTEST} stat ${n0} ctime`
	test_check $ctime1 -lt $ctime2
done
expect 0 unlink ${n0}

expect 0 symlink ${n1} ${n0}
for flag in UF_NODUMP UF_IMMUTABLE UF_APPEND UF_OPAQUE SF_ARCHIVED SF_IMMUTABLE SF_APPEND none; do
	ctime1=`${FSTEST} lstat ${n0} ctime`
	sleep 1
	expect 0 lchflags ${n0} ${flag}
	ctime2=`${FSTEST} lstat ${n0} ctime`
	test_check $ctime1 -lt $ctime2
done
expect 0 unlink ${n0}

# unsuccessful chflags(2) does not update ctime.
expect 0 create ${n0} 0644
for flag in UF_IMMUTABLE SF_IMMUTABLE none; do
	ctime1=`${FSTEST} stat ${n0} ctime`
	sleep 1
	expect EPERM -u 65534 chflags ${n0} ${flag}
	ctime2=`${FSTEST} stat ${n0} ctime`
	test_check $ctime1 -eq $ctime2
done
expect 0 unlink ${n0}

expect 0 mkdir ${n0} 0755
for flag in UF_IMMUTABLE SF_IMMUTABLE none; do
	ctime1=`${FSTEST} stat ${n0} ctime`
	sleep 1
	expect EPERM -u 65534 chflags ${n0} ${flag}
	ctime2=`${FSTEST} stat ${n0} ctime`
	test_check $ctime1 -eq $ctime2
done
expect 0 rmdir ${n0}

expect 0 mkfifo ${n0} 0644
for flag in UF_IMMUTABLE SF_IMMUTABLE none; do
	ctime1=`${FSTEST} stat ${n0} ctime`
	sleep 1
	expect EPERM -u 65534 chflags ${n0} ${flag}
	ctime2=`${FSTEST} stat ${n0} ctime`
	test_check $ctime1 -eq $ctime2
done
expect 0 unlink ${n0}

expect 0 symlink ${n1} ${n0}
for flag in UF_IMMUTABLE SF_IMMUTABLE none; do
	ctime1=`${FSTEST} lstat ${n0} ctime`
	sleep 1
	expect EPERM -u 65534 lchflags ${n0} ${flag}
	ctime2=`${FSTEST} lstat ${n0} ctime`
	test_check $ctime1 -eq $ctime2
done
expect 0 unlink ${n0}

cd ${cdir}
expect 0 rmdir ${n2}
