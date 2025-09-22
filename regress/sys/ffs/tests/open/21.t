#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/open/21.t,v 1.1 2007/01/17 01:42:10 pjd Exp $

desc="open returns EFAULT if the path argument points outside the process's allocated address space"

expect EFAULT open NULL O_RDONLY
expect EFAULT open DEADCODE O_RDONLY
