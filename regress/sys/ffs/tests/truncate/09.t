#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/truncate/09.t,v 1.1 2007/01/17 01:42:12 pjd Exp $

desc="truncate returns EISDIR if the named file is a directory"

n0=`namegen`

expect 0 mkdir ${n0} 0755
expect EISDIR truncate ${n0} 123
expect 0 rmdir ${n0}
