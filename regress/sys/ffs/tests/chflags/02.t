#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/chflags/02.t,v 1.1 2007/01/17 01:42:08 pjd Exp $

desc="chflags returns ENAMETOOLONG if a component of a pathname exceeded 255 characters"

expect 0 create ${name255} 0644
expect 0 chflags ${name255} UF_IMMUTABLE
expect UF_IMMUTABLE stat ${name255} flags
expect 0 chflags ${name255} none
expect 0 unlink ${name255}
expect ENAMETOOLONG chflags ${name256} UF_IMMUTABLE
