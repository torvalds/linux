#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/open/16.t,v 1.1 2007/01/17 01:42:10 pjd Exp $

desc="open returns ELOOP when O_NOFOLLOW was specified and the path argument names a symbolic link."

n0=`namegen`
n1=`namegen`

expect 0 symlink ${n0} ${n1}
expect ELOOP open ${n1} O_RDONLY,O_CREAT,O_NOFOLLOW 0644
expect ELOOP open ${n1} O_RDONLY,O_NOFOLLOW
expect ELOOP open ${n1} O_WRONLY,O_NOFOLLOW
expect ELOOP open ${n1} O_RDWR,O_NOFOLLOW
expect 0 unlink ${n1}
