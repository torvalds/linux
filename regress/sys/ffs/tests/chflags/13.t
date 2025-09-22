#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/chflags/13.t,v 1.1 2007/01/17 01:42:08 pjd Exp $

desc="chflags returns EFAULT if the path argument points outside the process's allocated address space"

expect EFAULT chflags NULL UF_IMMUTABLE
expect EFAULT chflags DEADCODE UF_IMMUTABLE
