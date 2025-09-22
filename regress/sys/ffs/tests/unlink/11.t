#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/unlink/11.t,v 1.1 2007/01/17 01:42:12 pjd Exp $

desc="unlink returns EACCES or EPERM if the directory containing the file is marked sticky, and neither the containing directory nor the file to be removed are owned by the effective user ID"

n0=`namegen`
n1=`namegen`
n2=`namegen`

expect 0 mkdir ${n2} 0755
cdir=`pwd`
cd ${n2}

expect 0 mkdir ${n0} 0755
expect 0 chown ${n0} 65534 65534
expect 0 chmod ${n0} 01777

# User owns both: the sticky directory and the file to be removed.
expect 0 -u 65534 -g 65534 create ${n0}/${n1} 0644
expect 0 -u 65534 -g 65534 unlink ${n0}/${n1}
# User owns the file to be removed, but doesn't own the sticky directory.
expect 0 -u 65533 -g 65533 create ${n0}/${n1} 0644
expect 0 -u 65533 -g 65533 unlink ${n0}/${n1}
# User owns the sticky directory, but doesn't own the file to be removed.
expect 0 -u 65533 -g 65533 create ${n0}/${n1} 0644
expect 0 -u 65534 -g 65534 unlink ${n0}/${n1}
# User doesn't own the sticky directory nor the file to be removed.
expect 0 -u 65534 -g 65534 create ${n0}/${n1} 0644
expect "EACCES|EPERM" -u 65533 -g 65533 unlink ${n0}/${n1}
expect 0 unlink ${n0}/${n1}

# User owns both: the sticky directory and the fifo to be removed.
expect 0 -u 65534 -g 65534 mkfifo ${n0}/${n1} 0644
expect 0 -u 65534 -g 65534 unlink ${n0}/${n1}
# User owns the fifo to be removed, but doesn't own the sticky directory.
expect 0 -u 65533 -g 65533 mkfifo ${n0}/${n1} 0644
expect 0 -u 65533 -g 65533 unlink ${n0}/${n1}
# User owns the sticky directory, but doesn't own the fifo to be removed.
expect 0 -u 65533 -g 65533 mkfifo ${n0}/${n1} 0644
expect 0 -u 65534 -g 65534 unlink ${n0}/${n1}
# User doesn't own the sticky directory nor the fifo to be removed.
expect 0 -u 65534 -g 65534 mkfifo ${n0}/${n1} 0644
expect "EACCES|EPERM" -u 65533 -g 65533 unlink ${n0}/${n1}
expect 0 unlink ${n0}/${n1}

# User owns both: the sticky directory and the symlink to be removed.
expect 0 -u 65534 -g 65534 symlink test ${n0}/${n1}
expect 0 -u 65534 -g 65534 unlink ${n0}/${n1}
# User owns the symlink to be removed, but doesn't own the sticky directory.
expect 0 -u 65533 -g 65533 symlink test ${n0}/${n1}
expect 0 -u 65533 -g 65533 unlink ${n0}/${n1}
# User owns the sticky directory, but doesn't own the symlink to be removed.
expect 0 -u 65533 -g 65533 symlink test ${n0}/${n1}
expect 0 -u 65534 -g 65534 unlink ${n0}/${n1}
# User doesn't own the sticky directory nor the symlink to be removed.
expect 0 -u 65534 -g 65534 symlink test ${n0}/${n1}
expect "EACCES|EPERM" -u 65533 -g 65533 unlink ${n0}/${n1}
expect 0 unlink ${n0}/${n1}

expect 0 rmdir ${n0}

cd ${cdir}
expect 0 rmdir ${n2}
