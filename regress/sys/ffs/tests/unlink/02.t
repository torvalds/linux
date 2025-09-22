#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/unlink/02.t,v 1.1 2007/01/17 01:42:12 pjd Exp $

desc="unlink returns ENAMETOOLONG if a component of a pathname exceeded 255 characters"

expect 0 create ${name255} 0644
expect 0 unlink ${name255}
expect ENOENT unlink ${name255}
expect ENAMETOOLONG unlink ${name256}
