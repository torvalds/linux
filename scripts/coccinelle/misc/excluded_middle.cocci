// SPDX-License-Identifier: GPL-2.0-only
///
/// Condition !A || A && B is equivalent to !A || B.
///
// Confidence: High
// Copyright: (C) 2020 Denis Efremov ISPRAS
// Options: --no-includes --include-headers

virtual patch
virtual context
virtual org
virtual report

@r depends on !patch@
expression A, B;
position p;
@@

* !A || (A &&@p B)

@depends on patch@
expression A, B;
@@

  !A ||
-       (A && B)
+       B

@script:python depends on report@
p << r.p;
@@

coccilib.report.print_report(p[0], "WARNING !A || A && B is equivalent to !A || B")

@script:python depends on org@
p << r.p;
@@

coccilib.org.print_todo(p[0], "WARNING !A || A && B is equivalent to !A || B")
