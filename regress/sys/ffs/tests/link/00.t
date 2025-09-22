#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/link/00.t,v 1.1 2007/01/17 01:42:09 pjd Exp $

desc="link creates hardlinks"

n0=`namegen`
n1=`namegen`
n2=`namegen`
n3=`namegen`

expect 0 mkdir ${n3} 0755
cdir=`pwd`
cd ${n3}

expect 0 create ${n0} 0644
expect regular,0644,1 lstat ${n0} type,mode,nlink

expect 0 link ${n0} ${n1}
expect regular,0644,2 lstat ${n0} type,mode,nlink
expect regular,0644,2 lstat ${n1} type,mode,nlink

expect 0 link ${n1} ${n2}
expect regular,0644,3 lstat ${n0} type,mode,nlink
expect regular,0644,3 lstat ${n1} type,mode,nlink
expect regular,0644,3 lstat ${n2} type,mode,nlink

expect 0 chmod ${n1} 0201
expect 0 chown ${n1} 65534 65533

expect regular,0201,3,65534,65533 lstat ${n0} type,mode,nlink,uid,gid
expect regular,0201,3,65534,65533 lstat ${n1} type,mode,nlink,uid,gid
expect regular,0201,3,65534,65533 lstat ${n2} type,mode,nlink,uid,gid

expect 0 unlink ${n0}
expect ENOENT lstat ${n0} type,mode,nlink,uid,gid
expect regular,0201,2,65534,65533 lstat ${n1} type,mode,nlink,uid,gid
expect regular,0201,2,65534,65533 lstat ${n2} type,mode,nlink,uid,gid

expect 0 unlink ${n2}
expect ENOENT lstat ${n0} type,mode,nlink,uid,gid
expect regular,0201,1,65534,65533 lstat ${n1} type,mode,nlink,uid,gid
expect ENOENT lstat ${n2} type,mode,nlink,uid,gid

expect 0 unlink ${n1}
expect ENOENT lstat ${n0} type,mode,nlink,uid,gid
expect ENOENT lstat ${n1} type,mode,nlink,uid,gid
expect ENOENT lstat ${n2} type,mode,nlink,uid,gid

expect 0 mkfifo ${n0} 0644
expect fifo,0644,1 lstat ${n0} type,mode,nlink

expect 0 link ${n0} ${n1}
expect fifo,0644,2 lstat ${n0} type,mode,nlink
expect fifo,0644,2 lstat ${n1} type,mode,nlink

expect 0 link ${n1} ${n2}
expect fifo,0644,3 lstat ${n0} type,mode,nlink
expect fifo,0644,3 lstat ${n1} type,mode,nlink
expect fifo,0644,3 lstat ${n2} type,mode,nlink

expect 0 chmod ${n1} 0201
expect 0 chown ${n1} 65534 65533

expect fifo,0201,3,65534,65533 lstat ${n0} type,mode,nlink,uid,gid
expect fifo,0201,3,65534,65533 lstat ${n1} type,mode,nlink,uid,gid
expect fifo,0201,3,65534,65533 lstat ${n2} type,mode,nlink,uid,gid

expect 0 unlink ${n0}
expect ENOENT lstat ${n0} type,mode,nlink,uid,gid
expect fifo,0201,2,65534,65533 lstat ${n1} type,mode,nlink,uid,gid
expect fifo,0201,2,65534,65533 lstat ${n2} type,mode,nlink,uid,gid

expect 0 unlink ${n2}
expect ENOENT lstat ${n0} type,mode,nlink,uid,gid
expect fifo,0201,1,65534,65533 lstat ${n1} type,mode,nlink,uid,gid
expect ENOENT lstat ${n2} type,mode,nlink,uid,gid

expect 0 unlink ${n1}
expect ENOENT lstat ${n0} type,mode,nlink,uid,gid
expect ENOENT lstat ${n1} type,mode,nlink,uid,gid
expect ENOENT lstat ${n2} type,mode,nlink,uid,gid

# successful link(2) updates ctime.
expect 0 create ${n0} 0644
ctime1=`${FSTEST} stat ${n0} ctime`
dctime1=`${FSTEST} stat . ctime`
dmtime1=`${FSTEST} stat . mtime`
sleep 1
expect 0 link ${n0} ${n1}
ctime2=`${FSTEST} stat ${n0} ctime`
test_check $ctime1 -lt $ctime2
dctime2=`${FSTEST} stat . ctime`
test_check $dctime1 -lt $dctime2
dmtime2=`${FSTEST} stat . mtime`
test_check $dctime1 -lt $dmtime2
expect 0 unlink ${n0}
expect 0 unlink ${n1}

expect 0 mkfifo ${n0} 0644
ctime1=`${FSTEST} stat ${n0} ctime`
dctime1=`${FSTEST} stat . ctime`
dmtime1=`${FSTEST} stat . mtime`
sleep 1
expect 0 link ${n0} ${n1}
ctime2=`${FSTEST} stat ${n0} ctime`
test_check $ctime1 -lt $ctime2
dctime2=`${FSTEST} stat . ctime`
test_check $dctime1 -lt $dctime2
dmtime2=`${FSTEST} stat . mtime`
test_check $dctime1 -lt $dmtime2
expect 0 unlink ${n0}
expect 0 unlink ${n1}

# unsuccessful link(2) does not update ctime.
expect 0 create ${n0} 0644
expect 0 chown ${n0} 65534 -1
ctime1=`${FSTEST} stat ${n0} ctime`
dctime1=`${FSTEST} stat . ctime`
dmtime1=`${FSTEST} stat . mtime`
sleep 1
expect EACCES -u 65534 link ${n0} ${n1}
ctime2=`${FSTEST} stat ${n0} ctime`
test_check $ctime1 -eq $ctime2
dctime2=`${FSTEST} stat . ctime`
test_check $dctime1 -eq $dctime2
dmtime2=`${FSTEST} stat . mtime`
test_check $dctime1 -eq $dmtime2
expect 0 unlink ${n0}

expect 0 mkfifo ${n0} 0644
expect 0 chown ${n0} 65534 -1
ctime1=`${FSTEST} stat ${n0} ctime`
dctime1=`${FSTEST} stat . ctime`
dmtime1=`${FSTEST} stat . mtime`
sleep 1
expect EACCES -u 65534 link ${n0} ${n1}
ctime2=`${FSTEST} stat ${n0} ctime`
test_check $ctime1 -eq $ctime2
dctime2=`${FSTEST} stat . ctime`
test_check $dctime1 -eq $dctime2
dmtime2=`${FSTEST} stat . mtime`
test_check $dctime1 -eq $dmtime2
expect 0 unlink ${n0}

cd ${cdir}
expect 0 rmdir ${n3}
