#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/rename/10.t,v 1.1 2007/01/17 01:42:10 pjd Exp $

desc="rename returns EACCES or EPERM if the file pointed at by the 'to' argument exists, the directory containing 'to' is marked sticky, and neither the containing directory nor 'to' are owned by the effective user ID"

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

expect 0 mkdir ${n1} 0755
expect 0 chmod ${n1} 01777

# User owns both: the sticky directory and the destination file.
expect 0 chown ${n1} 65534 65534
expect 0 -u 65534 -g 65534 create ${n0}/${n2} 0644
inode=`${FSTEST} lstat ${n0}/${n2} inode`
expect 0 -u 65534 -g 65534 create ${n1}/${n3} 0644
expect 0 -u 65534 -g 65534 rename ${n0}/${n2} ${n1}/${n3}
expect ENOENT lstat ${n0}/${n2} inode
expect ${inode} lstat ${n1}/${n3} inode
expect 0 -u 65534 -g 65534 mkfifo ${n0}/${n2} 0644
expect 0 -u 65534 -g 65534 rename ${n1}/${n3} ${n0}/${n2}
expect ${inode} lstat ${n0}/${n2} inode
expect ENOENT lstat ${n1}/${n3} inode
expect 0 -u 65534 -g 65534 symlink test ${n1}/${n3}
expect 0 -u 65534 -g 65534 rename ${n0}/${n2} ${n1}/${n3}
expect ENOENT lstat ${n0}/${n2} inode
expect ${inode} lstat ${n1}/${n3} inode
expect 0 unlink ${n1}/${n3}
# User owns the sticky directory, but doesn't own the destination file.
expect 0 chown ${n1} 65534 65534
expect 0 -u 65534 -g 65534 create ${n0}/${n2} 0644
inode=`${FSTEST} lstat ${n0}/${n2} inode`
expect 0 -u 65534 -g 65534 create ${n1}/${n3} 0644
expect 0 -u 65534 -g 65534 rename ${n0}/${n2} ${n1}/${n3}
expect ENOENT lstat ${n0}/${n2} type
expect ${inode} lstat ${n1}/${n3} inode
expect 0 -u 65534 -g 65534 mkfifo ${n0}/${n2} 0644
expect 0 -u 65534 -g 65534 rename ${n1}/${n3} ${n0}/${n2}
expect ${inode} lstat ${n0}/${n2} inode
expect ENOENT lstat ${n1}/${n3} inode
expect 0 -u 65534 -g 65534 symlink test ${n1}/${n3}
expect 0 -u 65534 -g 65534 rename ${n0}/${n2} ${n1}/${n3}
expect ENOENT lstat ${n0}/${n2} inode
expect ${inode} lstat ${n1}/${n3} inode
expect 0 unlink ${n1}/${n3}
# User owns the destination file, but doesn't own the sticky directory.
expect 0 chown ${n1} 65533 65533
expect 0 -u 65534 -g 65534 create ${n0}/${n2} 0644
inode=`${FSTEST} lstat ${n0}/${n2} inode`
expect 0 -u 65534 -g 65534 create ${n1}/${n3} 0644
expect 0 -u 65534 -g 65534 rename ${n0}/${n2} ${n1}/${n3}
expect ENOENT lstat ${n0}/${n2} type
expect ${inode} lstat ${n1}/${n3} inode
expect 0 -u 65534 -g 65534 mkfifo ${n0}/${n2} 0644
expect 0 -u 65534 -g 65534 rename ${n1}/${n3} ${n0}/${n2}
expect ${inode} lstat ${n0}/${n2} inode
expect ENOENT lstat ${n1}/${n3} inode
expect 0 -u 65534 -g 65534 symlink test ${n1}/${n3}
expect 0 -u 65534 -g 65534 rename ${n0}/${n2} ${n1}/${n3}
expect ENOENT lstat ${n0}/${n2} inode
expect ${inode} lstat ${n1}/${n3} inode
expect 0 unlink ${n1}/${n3}
# User doesn't own the sticky directory nor the destination file.
expect 0 chown ${n1} 65533 65533
expect 0 -u 65534 -g 65534 create ${n0}/${n2} 0644
expect 0 -u 65533 -g 65533 create ${n1}/${n3} 0644
inode=`${FSTEST} lstat ${n1}/${n3} inode`
expect "EACCES|EPERM" -u 65534 -g 65534 rename ${n0}/${n2} ${n1}/${n3}
expect ${inode} lstat ${n1}/${n3} inode
expect 0 unlink ${n0}/${n2}
expect 0 unlink ${n1}/${n3}

# User owns both: the sticky directory and the destination file.
expect 0 chown ${n1} 65534 65534
expect 0 -u 65534 -g 65534 mkdir ${n0}/${n2} 0755
expect 0 -u 65534 -g 65534 mkdir ${n1}/${n3} 0755
expect 0 -u 65534 -g 65534 rename ${n0}/${n2} ${n1}/${n3}
expect ENOENT lstat ${n0}/${n2} type
expect 0 rmdir ${n1}/${n3}
# User owns the sticky directory, but doesn't own the destination file.
expect 0 chown ${n1} 65534 65534
expect 0 -u 65534 -g 65534 mkdir ${n0}/${n2} 0755
expect 0 -u 65534 -g 65534 mkdir ${n1}/${n3} 0755
expect 0 -u 65534 -g 65534 rename ${n0}/${n2} ${n1}/${n3}
expect ENOENT lstat ${n0}/${n2} type
expect 0 rmdir ${n1}/${n3}
# User owns the destination file, but doesn't own the sticky directory.
expect 0 chown ${n1} 65533 65533
expect 0 -u 65534 -g 65534 mkdir ${n0}/${n2} 0755
expect 0 -u 65534 -g 65534 mkdir ${n1}/${n3} 0755
expect 0 -u 65534 -g 65534 rename ${n0}/${n2} ${n1}/${n3}
expect ENOENT lstat ${n0}/${n2} type
expect 0 rmdir ${n1}/${n3}
# User doesn't own the sticky directory nor the destination file.
expect 0 chown ${n1} 65533 65533
expect 0 -u 65534 -g 65534 mkdir ${n0}/${n2} 0755
expect 0 -u 65533 -g 65533 mkdir ${n1}/${n3} 0755
expect "EACCES|EPERM" -u 65534 -g 65534 rename ${n0}/${n2} ${n1}/${n3}
expect 0 rmdir ${n0}/${n2}
expect 0 rmdir ${n1}/${n3}

# User owns both: the sticky directory and the destination file.
expect 0 chown ${n1} 65534 65534
expect 0 -u 65534 -g 65534 mkfifo ${n0}/${n2} 0644
inode=`${FSTEST} lstat ${n0}/${n2} inode`
expect 0 -u 65534 -g 65534 mkfifo ${n1}/${n3} 0644
expect 0 -u 65534 -g 65534 rename ${n0}/${n2} ${n1}/${n3}
expect ENOENT lstat ${n0}/${n2} inode
expect ${inode} lstat ${n1}/${n3} inode
expect 0 -u 65534 -g 65534 create ${n0}/${n2} 0644
expect 0 -u 65534 -g 65534 rename ${n1}/${n3} ${n0}/${n2}
expect ${inode} lstat ${n0}/${n2} inode
expect ENOENT lstat ${n1}/${n3} inode
expect 0 -u 65534 -g 65534 symlink test ${n1}/${n3}
expect 0 -u 65534 -g 65534 rename ${n0}/${n2} ${n1}/${n3}
expect ENOENT lstat ${n0}/${n2} inode
expect ${inode} lstat ${n1}/${n3} inode
expect 0 unlink ${n1}/${n3}
# User owns the sticky directory, but doesn't own the destination file.
expect 0 chown ${n1} 65534 65534
expect 0 -u 65534 -g 65534 mkfifo ${n0}/${n2} 0644
inode=`${FSTEST} lstat ${n0}/${n2} inode`
expect 0 -u 65534 -g 65534 mkfifo ${n1}/${n3} 0644
expect 0 -u 65534 -g 65534 rename ${n0}/${n2} ${n1}/${n3}
expect ENOENT lstat ${n0}/${n2} type
expect ${inode} lstat ${n1}/${n3} inode
expect 0 -u 65534 -g 65534 create ${n0}/${n2} 0644
expect 0 -u 65534 -g 65534 rename ${n1}/${n3} ${n0}/${n2}
expect ${inode} lstat ${n0}/${n2} inode
expect ENOENT lstat ${n1}/${n3} inode
expect 0 -u 65534 -g 65534 symlink test ${n1}/${n3}
expect 0 -u 65534 -g 65534 rename ${n0}/${n2} ${n1}/${n3}
expect ENOENT lstat ${n0}/${n2} inode
expect ${inode} lstat ${n1}/${n3} inode
expect 0 unlink ${n1}/${n3}
# User owns the destination file, but doesn't own the sticky directory.
expect 0 chown ${n1} 65533 65533
expect 0 -u 65534 -g 65534 mkfifo ${n0}/${n2} 0644
inode=`${FSTEST} lstat ${n0}/${n2} inode`
expect 0 -u 65534 -g 65534 mkfifo ${n1}/${n3} 0644
expect 0 -u 65534 -g 65534 rename ${n0}/${n2} ${n1}/${n3}
expect ENOENT lstat ${n0}/${n2} type
expect ${inode} lstat ${n1}/${n3} inode
expect 0 -u 65534 -g 65534 create ${n0}/${n2} 0644
expect 0 -u 65534 -g 65534 rename ${n1}/${n3} ${n0}/${n2}
expect ${inode} lstat ${n0}/${n2} inode
expect ENOENT lstat ${n1}/${n3} inode
expect 0 -u 65534 -g 65534 symlink test ${n1}/${n3}
expect 0 -u 65534 -g 65534 rename ${n0}/${n2} ${n1}/${n3}
expect ENOENT lstat ${n0}/${n2} inode
expect ${inode} lstat ${n1}/${n3} inode
expect 0 unlink ${n1}/${n3}
# User doesn't own the sticky directory nor the destination file.
expect 0 chown ${n1} 65533 65533
expect 0 -u 65534 -g 65534 mkfifo ${n0}/${n2} 0644
expect 0 -u 65533 -g 65533 mkfifo ${n1}/${n3} 0644
inode=`${FSTEST} lstat ${n1}/${n3} inode`
expect "EACCES|EPERM" -u 65534 -g 65534 rename ${n0}/${n2} ${n1}/${n3}
expect ${inode} lstat ${n1}/${n3} inode
expect 0 unlink ${n0}/${n2}
expect 0 unlink ${n1}/${n3}

# User owns both: the sticky directory and the destination file.
expect 0 chown ${n1} 65534 65534
expect 0 -u 65534 -g 65534 symlink test ${n0}/${n2}
inode=`${FSTEST} lstat ${n0}/${n2} inode`
expect 0 -u 65534 -g 65534 symlink test ${n1}/${n3}
expect 0 -u 65534 -g 65534 rename ${n0}/${n2} ${n1}/${n3}
expect ENOENT lstat ${n0}/${n2} inode
expect ${inode} lstat ${n1}/${n3} inode
expect 0 -u 65534 -g 65534 create ${n0}/${n2} 0644
expect 0 -u 65534 -g 65534 rename ${n1}/${n3} ${n0}/${n2}
expect ${inode} lstat ${n0}/${n2} inode
expect ENOENT lstat ${n1}/${n3} inode
expect 0 -u 65534 -g 65534 mkfifo ${n1}/${n3} 0644
expect 0 -u 65534 -g 65534 rename ${n0}/${n2} ${n1}/${n3}
expect ENOENT lstat ${n0}/${n2} inode
expect ${inode} lstat ${n1}/${n3} inode
expect 0 unlink ${n1}/${n3}
# User owns the sticky directory, but doesn't own the destination file.
expect 0 chown ${n1} 65534 65534
expect 0 -u 65534 -g 65534 symlink test ${n0}/${n2}
inode=`${FSTEST} lstat ${n0}/${n2} inode`
expect 0 -u 65534 -g 65534 symlink test ${n1}/${n3}
expect 0 -u 65534 -g 65534 rename ${n0}/${n2} ${n1}/${n3}
expect ENOENT lstat ${n0}/${n2} type
expect ${inode} lstat ${n1}/${n3} inode
expect 0 -u 65534 -g 65534 create ${n0}/${n2} 0644
expect 0 -u 65534 -g 65534 rename ${n1}/${n3} ${n0}/${n2}
expect ${inode} lstat ${n0}/${n2} inode
expect ENOENT lstat ${n1}/${n3} inode
expect 0 -u 65534 -g 65534 mkfifo ${n1}/${n3} 0644
expect 0 -u 65534 -g 65534 rename ${n0}/${n2} ${n1}/${n3}
expect ENOENT lstat ${n0}/${n2} inode
expect ${inode} lstat ${n1}/${n3} inode
expect 0 unlink ${n1}/${n3}
# User owns the destination file, but doesn't own the sticky directory.
expect 0 chown ${n1} 65533 65533
expect 0 -u 65534 -g 65534 symlink test ${n0}/${n2}
inode=`${FSTEST} lstat ${n0}/${n2} inode`
expect 0 -u 65534 -g 65534 symlink test ${n1}/${n3}
expect 0 -u 65534 -g 65534 rename ${n0}/${n2} ${n1}/${n3}
expect ENOENT lstat ${n0}/${n2} type
expect ${inode} lstat ${n1}/${n3} inode
expect 0 -u 65534 -g 65534 create ${n0}/${n2} 0644
expect 0 -u 65534 -g 65534 rename ${n1}/${n3} ${n0}/${n2}
expect ${inode} lstat ${n0}/${n2} inode
expect ENOENT lstat ${n1}/${n3} inode
expect 0 -u 65534 -g 65534 mkfifo ${n1}/${n3} 0644
expect 0 -u 65534 -g 65534 rename ${n0}/${n2} ${n1}/${n3}
expect ENOENT lstat ${n0}/${n2} inode
expect ${inode} lstat ${n1}/${n3} inode
expect 0 unlink ${n1}/${n3}
# User doesn't own the sticky directory nor the destination file.
expect 0 chown ${n1} 65533 65533
expect 0 -u 65534 -g 65534 symlink test ${n0}/${n2}
expect 0 -u 65533 -g 65533 symlink test ${n1}/${n3}
inode=`${FSTEST} lstat ${n1}/${n3} inode`
expect "EACCES|EPERM" -u 65534 -g 65534 rename ${n0}/${n2} ${n1}/${n3}
expect ${inode} lstat ${n1}/${n3} inode
expect 0 unlink ${n0}/${n2}
expect 0 unlink ${n1}/${n3}

expect 0 rmdir ${n1}
expect 0 rmdir ${n0}

cd ${cdir}
expect 0 rmdir ${n4}
