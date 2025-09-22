#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/open/06.t,v 1.1 2007/01/17 01:42:10 pjd Exp $

desc="open returns EACCES when the required permissions (for reading and/or writing) are denied for the given flags"

n0=`namegen`
n1=`namegen`

expect 0 mkdir ${n0} 0755
expect 0 chown ${n0} 65534 65534
cdir=`pwd`
cd ${n0}

expect 0 -u 65534 -g 65534 create ${n1} 0644

expect 0 -u 65534 -g 65534 chmod ${n1} 0600
expect 0 -u 65534 -g 65534 open ${n1} O_RDONLY
expect 0 -u 65534 -g 65534 open ${n1} O_WRONLY
expect 0 -u 65534 -g 65534 open ${n1} O_RDWR
expect 0 -u 65534 -g 65534 chmod ${n1} 0060
expect 0 -u 65533 -g 65534 open ${n1} O_RDONLY
expect 0 -u 65533 -g 65534 open ${n1} O_WRONLY
expect 0 -u 65533 -g 65534 open ${n1} O_RDWR
expect 0 -u 65534 -g 65534 chmod ${n1} 0006
expect 0 -u 65533 -g 65533 open ${n1} O_RDONLY
expect 0 -u 65533 -g 65533 open ${n1} O_WRONLY
expect 0 -u 65533 -g 65533 open ${n1} O_RDWR

expect 0 -u 65534 -g 65534 chmod ${n1} 0477
expect 0 -u 65534 -g 65534 open ${n1} O_RDONLY
expect EACCES -u 65534 -g 65534 open ${n1} O_WRONLY
expect EACCES -u 65534 -g 65534 open ${n1} O_RDWR
expect 0 -u 65534 -g 65534 chmod ${n1} 0747
expect 0 -u 65533 -g 65534 open ${n1} O_RDONLY
expect EACCES -u 65533 -g 65534 open ${n1} O_WRONLY
expect EACCES -u 65533 -g 65534 open ${n1} O_RDWR
expect 0 -u 65534 -g 65534 chmod ${n1} 0774
expect 0 -u 65533 -g 65533 open ${n1} O_RDONLY
expect EACCES -u 65533 -g 65533 open ${n1} O_WRONLY
expect EACCES -u 65533 -g 65533 open ${n1} O_RDWR

expect 0 -u 65534 -g 65534 chmod ${n1} 0277
expect EACCES -u 65534 -g 65534 open ${n1} O_RDONLY
expect 0 -u 65534 -g 65534 open ${n1} O_WRONLY
expect EACCES -u 65534 -g 65534 open ${n1} O_RDWR
expect 0 -u 65534 -g 65534 chmod ${n1} 0727
expect EACCES -u 65533 -g 65534 open ${n1} O_RDONLY
expect 0 -u 65533 -g 65534 open ${n1} O_WRONLY
expect EACCES -u 65533 -g 65534 open ${n1} O_RDWR
expect 0 -u 65534 -g 65534 chmod ${n1} 0772
expect EACCES -u 65533 -g 65533 open ${n1} O_RDONLY
expect 0 -u 65533 -g 65533 open ${n1} O_WRONLY
expect EACCES -u 65533 -g 65533 open ${n1} O_RDWR

expect 0 -u 65534 -g 65534 chmod ${n1} 0177
expect EACCES -u 65534 -g 65534 open ${n1} O_RDONLY
expect EACCES -u 65534 -g 65534 open ${n1} O_WRONLY
expect EACCES -u 65534 -g 65534 open ${n1} O_RDWR
expect 0 -u 65534 -g 65534 chmod ${n1} 0717
expect EACCES -u 65533 -g 65534 open ${n1} O_RDONLY
expect EACCES -u 65533 -g 65534 open ${n1} O_WRONLY
expect EACCES -u 65533 -g 65534 open ${n1} O_RDWR
expect 0 -u 65534 -g 65534 chmod ${n1} 0771
expect EACCES -u 65533 -g 65533 open ${n1} O_RDONLY
expect EACCES -u 65533 -g 65533 open ${n1} O_WRONLY
expect EACCES -u 65533 -g 65533 open ${n1} O_RDWR

expect 0 -u 65534 -g 65534 chmod ${n1} 0077
expect EACCES -u 65534 -g 65534 open ${n1} O_RDONLY
expect EACCES -u 65534 -g 65534 open ${n1} O_WRONLY
expect EACCES -u 65534 -g 65534 open ${n1} O_RDWR
expect 0 -u 65534 -g 65534 chmod ${n1} 0707
expect EACCES -u 65533 -g 65534 open ${n1} O_RDONLY
expect EACCES -u 65533 -g 65534 open ${n1} O_WRONLY
expect EACCES -u 65533 -g 65534 open ${n1} O_RDWR
expect 0 -u 65534 -g 65534 chmod ${n1} 0770
expect EACCES -u 65533 -g 65533 open ${n1} O_RDONLY
expect EACCES -u 65533 -g 65533 open ${n1} O_WRONLY
expect EACCES -u 65533 -g 65533 open ${n1} O_RDWR

expect 0 -u 65534 -g 65534 unlink ${n1}

cd ${cdir}
expect 0 rmdir ${n0}
