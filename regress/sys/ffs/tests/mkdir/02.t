#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/mkdir/02.t,v 1.1 2007/01/17 01:42:09 pjd Exp $

desc="mkdir returns ENAMETOOLONG if a component of a pathname exceeded 255 characters"

expect 0 mkdir ${name255} 0755
expect 0 rmdir ${name255}
expect ENAMETOOLONG mkdir ${name256} 0755
