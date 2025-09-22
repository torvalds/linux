#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/truncate/03.t,v 1.1 2007/01/17 01:42:12 pjd Exp $

desc="truncate returns ENAMETOOLONG if an entire path name exceeded 1023 characters"

expect 0 mkdir ${name255} 0755
expect 0 mkdir ${name255}/${name255} 0755
expect 0 mkdir ${name255}/${name255}/${name255} 0755
expect 0 mkdir ${path1021} 0755
expect 0 create ${path1023} 0644
expect 0 truncate ${path1023} 123
expect 0 unlink ${path1023}
expect ENAMETOOLONG truncate ${path1024} 123
expect 0 rmdir ${path1021}
expect 0 rmdir ${name255}/${name255}/${name255}
expect 0 rmdir ${name255}/${name255}
expect 0 rmdir ${name255}
