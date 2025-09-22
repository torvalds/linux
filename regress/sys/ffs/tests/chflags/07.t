#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/chflags/07.t,v 1.1 2007/01/17 01:42:08 pjd Exp $

desc="chflags returns EPERM when the effective user ID does not match the owner of the file and the effective user ID is not the super-user"

n0=`namegen`
n1=`namegen`
n2=`namegen`

expect 0 mkdir ${n0} 0755
cdir=`pwd`
cd ${n0}

expect 0 create ${n1} 0644
expect EPERM -u 65534 -g 65534 chflags ${n1} UF_IMMUTABLE
expect none stat ${n1} flags
expect 0 chown ${n1} 65534 65534
expect EPERM -u 65533 -g 65533 chflags ${n1} UF_IMMUTABLE
expect none stat ${n1} flags
expect 0 unlink ${n1}

expect 0 mkdir ${n1} 0755
expect EPERM -u 65534 -g 65534 chflags ${n1} UF_IMMUTABLE
expect none stat ${n1} flags
expect 0 chown ${n1} 65534 65534
expect EPERM -u 65533 -g 65533 chflags ${n1} UF_IMMUTABLE
expect none stat ${n1} flags
expect 0 rmdir ${n1}

expect 0 mkfifo ${n1} 0644
expect EPERM -u 65534 -g 65534 chflags ${n1} UF_IMMUTABLE
expect none stat ${n1} flags
expect 0 chown ${n1} 65534 65534
expect EPERM -u 65533 -g 65533 chflags ${n1} UF_IMMUTABLE
expect none stat ${n1} flags
expect 0 unlink ${n1}

expect 0 symlink ${n2} ${n1}
expect EPERM -u 65534 -g 65534 lchflags ${n1} UF_IMMUTABLE
expect none lstat ${n1} flags
expect 0 lchown ${n1} 65534 65534
expect EPERM -u 65533 -g 65533 lchflags ${n1} UF_IMMUTABLE
expect none lstat ${n1} flags
expect 0 unlink ${n1}

cd ${cdir}
expect 0 rmdir ${n0}
