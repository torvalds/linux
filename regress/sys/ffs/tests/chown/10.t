#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/chown/10.t,v 1.1 2007/01/17 01:42:08 pjd Exp $

desc="chown returns EFAULT if the path argument points outside the process's allocated address space"

expect EFAULT chown NULL 65534 65534
expect EFAULT chown DEADCODE 65534 65534
