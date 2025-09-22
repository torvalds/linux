#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/truncate/11.t,v 1.1 2007/01/17 01:42:12 pjd Exp $

desc="truncate returns ETXTBSY the file is a pure procedure (shared text) file that is being executed"

n0=`namegen`

cp -pf `which sleep` ${n0}
./${n0} 3 &
expect ETXTBSY truncate ${n0} 123
expect 0 unlink ${n0}
