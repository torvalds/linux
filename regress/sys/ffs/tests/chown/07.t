#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/chown/07.t,v 1.1 2007/01/17 01:42:08 pjd Exp $

desc="chown returns EPERM if the operation would change the ownership, but the effective user ID is not the super-user and the process is not an owner of the file"

n0=`namegen`
n1=`namegen`
n2=`namegen`

expect 0 mkdir ${n0} 0755
cdir=`pwd`
cd ${n0}
expect 0 mkdir ${n1} 0755
expect 0 chown ${n1} 65534 65534
expect 0 -u 65534 -g 65534 create ${n1}/${n2} 0644
expect EPERM -u 65534 -g 65534 chown ${n1}/${n2} 65533 65533
expect EPERM -u 65533 -g 65533 chown ${n1}/${n2} 65534 65534
expect EPERM -u 65533 -g 65533 chown ${n1}/${n2} 65533 65533
expect EPERM -u 65534 -g 65534 chown ${n1}/${n2} -1 65533
expect 0 unlink ${n1}/${n2}
expect 0 rmdir ${n1}
cd ${cdir}
expect 0 rmdir ${n0}
