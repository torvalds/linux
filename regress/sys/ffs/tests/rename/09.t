#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/rename/09.t,v 1.1 2007/01/17 01:42:10 pjd Exp $

desc="rename returns EACCES or EPERM if the directory containing 'from' is marked sticky, and neither the containing directory nor 'from' are owned by the effective user ID"

n0=`namegen`
n1=`namegen`
n2=`namegen`
n3=`namegen`
n4=`namegen`

expect 0 mkdir ${n4} 0755
cdir=`pwd`
cd ${n4}

expect 0 mkdir ${n0} 0755
expect 0 chown ${n0} 65534 65534
expect 0 chmod ${n0} 01777

expect 0 mkdir ${n1} 0755

# User owns both: the sticky directory and the file to be renamed.
expect 0 chown ${n1} 65534 65534
expect 0 -u 65534 -g 65534 create ${n0}/${n2} 0644
expect 0 -u 65534 -g 65534 rename ${n0}/${n2} ${n1}/${n3}
expect 0 unlink ${n1}/${n3}
# User owns the file to be renamed, but doesn't own the sticky directory.
expect 0 chown ${n1} 65533 65533
expect 0 -u 65533 -g 65533 create ${n0}/${n2} 0644
expect 0 -u 65533 -g 65533 rename ${n0}/${n2} ${n1}/${n3}
expect 0 unlink ${n1}/${n3}
# User owns the sticky directory, but doesn't own the file to be renamed.
expect 0 chown ${n1} 65534 65534
expect 0 -u 65533 -g 65533 create ${n0}/${n2} 0644
expect 0 -u 65534 -g 65534 rename ${n0}/${n2} ${n1}/${n3}
expect 0 unlink ${n1}/${n3}
# User doesn't own the sticky directory nor the file to be renamed.
expect 0 chown ${n1} 65533 65533
expect 0 -u 65534 -g 65534 create ${n0}/${n2} 0644
expect "EACCES|EPERM" -u 65533 -g 65533 rename ${n0}/${n2} ${n1}/${n3}
expect 0 unlink ${n0}/${n2}

# User owns both: the sticky directory and the fifo to be renamed.
expect 0 chown ${n1} 65534 65534
expect 0 -u 65534 -g 65534 mkfifo ${n0}/${n2} 0644
expect 0 -u 65534 -g 65534 rename ${n0}/${n2} ${n1}/${n3}
expect 0 unlink ${n1}/${n3}
# User owns the fifo to be renamed, but doesn't own the sticky directory.
expect 0 chown ${n1} 65533 65533
expect 0 -u 65533 -g 65533 mkfifo ${n0}/${n2} 0644
expect 0 -u 65533 -g 65533 rename ${n0}/${n2} ${n1}/${n3}
expect 0 unlink ${n1}/${n3}
# User owns the sticky directory, but doesn't own the fifo to be renamed.
expect 0 chown ${n1} 65534 65534
expect 0 -u 65533 -g 65533 mkfifo ${n0}/${n2} 0644
expect 0 -u 65534 -g 65534 rename ${n0}/${n2} ${n1}/${n3}
expect 0 unlink ${n1}/${n3}
# User doesn't own the sticky directory nor the fifo to be renamed.
expect 0 chown ${n1} 65533 65533
expect 0 -u 65534 -g 65534 mkfifo ${n0}/${n2} 0644
expect "EACCES|EPERM" -u 65533 -g 65533 rename ${n0}/${n2} ${n1}/${n3}
expect 0 unlink ${n0}/${n2}

# User owns both: the sticky directory and the symlink to be renamed.
expect 0 chown ${n1} 65534 65534
expect 0 -u 65534 -g 65534 symlink test ${n0}/${n2}
expect 0 -u 65534 -g 65534 rename ${n0}/${n2} ${n1}/${n3}
expect 0 unlink ${n1}/${n3}
# User owns the symlink to be renamed, but doesn't own the sticky directory.
expect 0 chown ${n1} 65533 65533
expect 0 -u 65533 -g 65533 symlink test ${n0}/${n2}
expect 0 -u 65533 -g 65533 rename ${n0}/${n2} ${n1}/${n3}
expect 0 unlink ${n1}/${n3}
# User owns the sticky directory, but doesn't own the symlink to be renamed.
expect 0 chown ${n1} 65534 65534
expect 0 -u 65533 -g 65533 symlink test ${n0}/${n2}
expect 0 -u 65534 -g 65534 rename ${n0}/${n2} ${n1}/${n3}
expect 0 unlink ${n1}/${n3}
# User doesn't own the sticky directory nor the symlink to be renamed.
expect 0 chown ${n1} 65533 65533
expect 0 -u 65534 -g 65534 symlink test ${n0}/${n2}
expect "EACCES|EPERM" -u 65533 -g 65533 rename ${n0}/${n2} ${n1}/${n3}
expect 0 unlink ${n0}/${n2}

expect 0 rmdir ${n1}
expect 0 rmdir ${n0}

cd ${cdir}
expect 0 rmdir ${n4}
