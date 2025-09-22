#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/open/17.t,v 1.1 2007/01/17 01:42:10 pjd Exp $

desc="open returns ENXIO when O_NONBLOCK is set, the named file is a fifo, O_WRONLY is set, and no process has the file open for reading"

n0=`namegen`

expect 0 mkfifo ${n0} 0644
expect ENXIO open ${n0} O_WRONLY,O_NONBLOCK
expect 0 unlink ${n0}
