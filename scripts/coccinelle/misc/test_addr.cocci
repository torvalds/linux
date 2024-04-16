// SPDX-License-Identifier: GPL-2.0-only
/// The address of a variable or field is likely always to be non-zero.
///
// Confidence: High
// Copyright: (C) 2012 Julia Lawall, INRIA/LIP6.
// Copyright: (C) 2012 Gilles Muller, INRIA/LiP6.
// URL: https://coccinelle.gitlabpages.inria.fr/website
// Comments:
// Options: --no-includes --include-headers

virtual org
virtual report
virtual context

@r@
expression x;
position p;
@@

*\(&x@p == NULL \| &x@p != NULL\)

@script:python depends on org@
p << r.p;
@@

cocci.print_main("test of a variable/field address",p)

@script:python depends on report@
p << r.p;
@@

msg = "ERROR: test of a variable/field address"
coccilib.report.print_report(p[0],msg)
