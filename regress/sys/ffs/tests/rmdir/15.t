#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/rmdir/15.t,v 1.1 2007/01/17 01:42:11 pjd Exp $

desc="rmdir returns EFAULT if the path argument points outside the process's allocated address space"

expect EFAULT rmdir NULL
expect EFAULT rmdir DEADCODE
