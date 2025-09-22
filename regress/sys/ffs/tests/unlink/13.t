#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/unlink/13.t,v 1.1 2007/01/17 01:42:12 pjd Exp $

desc="unlink returns EFAULT if the path argument points outside the process's allocated address space"

expect EFAULT unlink NULL
expect EFAULT unlink DEADCODE
