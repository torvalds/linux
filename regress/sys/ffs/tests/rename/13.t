#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/rename/13.t,v 1.1 2007/01/17 01:42:10 pjd Exp $

desc="rename returns ENOTDIR when the 'from' argument is a directory, but 'to' is not a directory"

n0=`namegen`
n1=`namegen`

expect 0 mkdir ${n0} 0755

expect 0 create ${n1} 0644
expect ENOTDIR rename ${n0} ${n1}
expect dir lstat ${n0} type
expect regular lstat ${n1} type
expect 0 unlink ${n1}

expect 0 mkfifo ${n1} 0644
expect ENOTDIR rename ${n0} ${n1}
expect dir lstat ${n0} type
expect fifo lstat ${n1} type
expect 0 unlink ${n1}

expect 0 symlink test ${n1}
expect ENOTDIR rename ${n0} ${n1}
expect dir lstat ${n0} type
expect symlink lstat ${n1} type
expect 0 unlink ${n1}

expect 0 rmdir ${n0}
