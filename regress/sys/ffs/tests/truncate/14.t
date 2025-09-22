#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/truncate/14.t,v 1.1 2007/01/17 01:42:12 pjd Exp $

desc="truncate returns EFAULT if the path argument points outside the process's allocated address space"

expect EFAULT truncate NULL 123
expect EFAULT truncate DEADCODE 123
