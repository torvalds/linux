#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/mkfifo/12.t,v 1.1 2007/01/17 01:42:10 pjd Exp $

desc="mkfifo returns EFAULT if the path argument points outside the process's allocated address space"

expect EFAULT mkfifo NULL 0644
expect EFAULT mkfifo DEADCODE 0644
