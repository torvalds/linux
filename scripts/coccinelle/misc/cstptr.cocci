// SPDX-License-Identifier: GPL-2.0-only
/// PTR_ERR should be applied before its argument is reassigned, typically
/// to NULL
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

@r exists@
expression e,e1;
constant c;
position p1,p2;
@@

*e@p1 = c
... when != e = e1
    when != &e
    when != true IS_ERR(e)
*PTR_ERR@p2(e)

@script:python depends on org@
p1 << r.p1;
p2 << r.p2;
@@

cocci.print_main("PTR_ERR",p2)
cocci.print_secs("assignment",p1)

@script:python depends on report@
p1 << r.p1;
p2 << r.p2;
@@

msg = "ERROR: PTR_ERR applied after initialization to constant on line %s" % (p1[0].line)
coccilib.report.print_report(p2[0],msg)
