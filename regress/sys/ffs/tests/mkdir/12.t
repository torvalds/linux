#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/mkdir/12.t,v 1.1 2007/01/17 01:42:09 pjd Exp $

desc="mkdir returns EFAULT if the path argument points outside the process's allocated address space"

expect EFAULT mkdir NULL 0755
expect EFAULT mkdir DEADCODE 0755
