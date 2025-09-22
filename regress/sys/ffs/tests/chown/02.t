#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/chown/02.t,v 1.1 2007/01/17 01:42:08 pjd Exp $

desc="chown returns ENAMETOOLONG if a component of a pathname exceeded 255 characters"
expect 0 create ${name255} 0644
expect 0 chown ${name255} 65534 65534
expect 65534,65534 stat ${name255} uid,gid
expect 0 unlink ${name255}
expect ENAMETOOLONG chown ${name256} 65533 65533
