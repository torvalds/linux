#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/truncate/00.t,v 1.1 2007/01/17 01:42:12 pjd Exp $

desc="truncate descrease/increase file size"

n0=`namegen`
n1=`namegen`

expect 0 mkdir ${n1} 0755
cdir=`pwd`
cd ${n1}

expect 0 create ${n0} 0644
expect 0 truncate ${n0} 1234567
expect 1234567 lstat ${n0} size
expect 0 truncate ${n0} 567
expect 567 lstat ${n0} size
expect 0 unlink ${n0}

dd if=/dev/random of=${n0} bs=12345 count=1 status=none
expect 0 truncate ${n0} 23456
expect 23456 lstat ${n0} size
expect 0 truncate ${n0} 1
expect 1 lstat ${n0} size
expect 0 unlink ${n0}

# successful truncate(2) updates ctime.
expect 0 create ${n0} 0644
ctime1=`${FSTEST} stat ${n0} ctime`
sleep 1
expect 0 truncate ${n0} 123
ctime2=`${FSTEST} stat ${n0} ctime`
test_check $ctime1 -lt $ctime2
expect 0 unlink ${n0}

# unsuccessful truncate(2) does not update ctime.
expect 0 create ${n0} 0644
ctime1=`${FSTEST} stat ${n0} ctime`
sleep 1
expect EACCES -u 65534 truncate ${n0} 123
ctime2=`${FSTEST} stat ${n0} ctime`
test_check $ctime1 -eq $ctime2
expect 0 unlink ${n0}

cd ${cdir}
expect 0 rmdir ${n1}
