#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/open/02.t,v 1.1 2007/01/17 01:42:10 pjd Exp $

desc="open returns ENAMETOOLONG if a component of a pathname exceeded 255 characters"

expect 0 open ${name255} O_CREAT 0620
expect 0620 stat ${name255} mode
expect 0 unlink ${name255}
expect ENAMETOOLONG open ${name256} O_CREAT 0620
