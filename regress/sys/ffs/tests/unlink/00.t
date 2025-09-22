#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/unlink/00.t,v 1.1 2007/01/17 01:42:12 pjd Exp $

desc="unlink removes regular files, symbolic links, fifos and sockets"

n0=`namegen`
n1=`namegen`
n2=`namegen`

expect 0 mkdir ${n2} 0755
cdir=`pwd`
cd ${n2}

expect 0 create ${n0} 0644
expect regular lstat ${n0} type
expect 0 unlink ${n0}
expect ENOENT lstat ${n0} type

expect 0 symlink ${n1} ${n0}
expect symlink lstat ${n0} type
expect 0 unlink ${n0}
expect ENOENT lstat ${n0} type

expect 0 mkfifo ${n0} 0644
expect fifo lstat ${n0} type
expect 0 unlink ${n0}
expect ENOENT lstat ${n0} type

# TODO: sockets removal

# successful unlink(2) updates ctime.
expect 0 create ${n0} 0644
expect 0 link ${n0} ${n1}
ctime1=`${FSTEST} stat ${n0} ctime`
sleep 1
expect 0 unlink ${n1}
ctime2=`${FSTEST} stat ${n0} ctime`
test_check $ctime1 -lt $ctime2
expect 0 unlink ${n0}

expect 0 mkfifo ${n0} 0644
expect 0 link ${n0} ${n1}
ctime1=`${FSTEST} stat ${n0} ctime`
sleep 1
expect 0 unlink ${n1}
ctime2=`${FSTEST} stat ${n0} ctime`
test_check $ctime1 -lt $ctime2
expect 0 unlink ${n0}

# unsuccessful unlink(2) does not update ctime.
expect 0 create ${n0} 0644
ctime1=`${FSTEST} stat ${n0} ctime`
sleep 1
expect EACCES -u 65534 unlink ${n0}
ctime2=`${FSTEST} stat ${n0} ctime`
test_check $ctime1 -eq $ctime2
expect 0 unlink ${n0}

expect 0 mkfifo ${n0} 0644
ctime1=`${FSTEST} stat ${n0} ctime`
sleep 1
expect EACCES -u 65534 unlink ${n0}
ctime2=`${FSTEST} stat ${n0} ctime`
test_check $ctime1 -eq $ctime2
expect 0 unlink ${n0}

expect 0 mkdir ${n0} 0755
expect 0 create ${n0}/${n1} 0644
time=`${FSTEST} stat ${n0} ctime`
sleep 1
expect 0 unlink ${n0}/${n1}
mtime=`${FSTEST} stat ${n0} mtime`
test_check $time -lt $mtime
ctime=`${FSTEST} stat ${n0} ctime`
test_check $time -lt $ctime
expect 0 rmdir ${n0}

expect 0 mkdir ${n0} 0755
expect 0 mkfifo ${n0}/${n1} 0644
time=`${FSTEST} stat ${n0} ctime`
sleep 1
expect 0 unlink ${n0}/${n1}
mtime=`${FSTEST} stat ${n0} mtime`
test_check $time -lt $mtime
ctime=`${FSTEST} stat ${n0} ctime`
test_check $time -lt $ctime
expect 0 rmdir ${n0}

expect 0 mkdir ${n0} 0755
expect 0 symlink test ${n0}/${n1}
time=`${FSTEST} stat ${n0} ctime`
sleep 1
expect 0 unlink ${n0}/${n1}
mtime=`${FSTEST} stat ${n0} mtime`
test_check $time -lt $mtime
ctime=`${FSTEST} stat ${n0} ctime`
test_check $time -lt $ctime
expect 0 rmdir ${n0}

expect 0 create ${n0} 0644
expect 0 link ${n0} ${n1}
time=`${FSTEST} stat ${n0} ctime`
sleep 1
expect 0 unlink ${n1}
ctime=`${FSTEST} stat ${n0} ctime`
test_check $time -lt $ctime
expect 0 unlink ${n0}

cd ${cdir}
expect 0 rmdir ${n2}
