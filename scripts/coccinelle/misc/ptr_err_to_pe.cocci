// SPDX-License-Identifier: GPL-2.0-only
/// Use %pe format specifier instead of PTR_ERR() for printing error pointers.
///
/// For printing error pointers (i.e., a pointer for which IS_ERR() is true)
/// %pe will print a symbolic error name (e.g., -EINVAL), opposed to the raw
/// errno (e.g., -22) produced by PTR_ERR().
/// It also makes the code cleaner by saving a redundant call to PTR_ERR().
///
// Confidence: High
// Copyright: (C) 2025 NVIDIA CORPORATION & AFFILIATES.
// URL: https://coccinelle.gitlabpages.inria.fr/website
// Options: --no-includes --include-headers

virtual context
virtual org
virtual report

@r@
expression ptr;
constant fmt;
position p;
identifier print_func;
@@
* print_func(..., fmt, ..., PTR_ERR@p(ptr), ...)

@script:python depends on r && report@
p << r.p;
@@
coccilib.report.print_report(p[0], "WARNING: Consider using %pe to print PTR_ERR()")

@script:python depends on r && org@
p << r.p;
@@
coccilib.org.print_todo(p[0], "WARNING: Consider using %pe to print PTR_ERR()")
