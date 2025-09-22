#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/open/18.t,v 1.1 2007/01/17 01:42:10 pjd Exp $

desc="open returns EWOULDBLOCK when O_NONBLOCK and one of O_SHLOCK or O_EXLOCK is specified and the file is locked"

n0=`namegen`

expect 0 create ${n0} 0644
expect 0 open ${n0} O_RDONLY,O_SHLOCK : open ${n0} O_RDONLY,O_SHLOCK,O_NONBLOCK
expect "EWOULDBLOCK|EAGAIN" open ${n0} O_RDONLY,O_EXLOCK : open ${n0} O_RDONLY,O_EXLOCK,O_NONBLOCK
expect "EWOULDBLOCK|EAGAIN" open ${n0} O_RDONLY,O_SHLOCK : open ${n0} O_RDONLY,O_EXLOCK,O_NONBLOCK
expect "EWOULDBLOCK|EAGAIN" open ${n0} O_RDONLY,O_EXLOCK : open ${n0} O_RDONLY,O_SHLOCK,O_NONBLOCK
expect 0 unlink ${n0}
