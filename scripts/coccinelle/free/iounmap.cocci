// SPDX-License-Identifier: GPL-2.0-only
/// Find missing iounmaps.
///
//# This only signals a missing iounmap when there is an iounmap later
//# in the same function.
//# False positives can be due to loops.
//
// Confidence: Moderate
// Copyright: (C) 2012 Julia Lawall, INRIA/LIP6.
// Copyright: (C) 2012 Gilles Muller, INRIA/LiP6.
// URL: https://coccinelle.gitlabpages.inria.fr/website
// Comments:
// Options:

virtual context
virtual org
virtual report

@iom@
expression e;
statement S,S1;
int ret;
position p1,p2,p3;
@@

e = \(ioremap@p1\)(...)
... when != iounmap(e)
if (<+...e...+>) S
... when any
    when != iounmap(e)
    when != if (...) { ... iounmap(e); ... }
(
 if (ret == 0) S1
|
if (...)
   { ...
     return 0; }
|
if (...)
   { ...
     return <+...e...+>; }
|
*if@p2 (...)
   { ... when != iounmap(e)
         when forall
     return@p3 ...; }
)
... when any
iounmap(e);

@script:python depends on org@
p1 << iom.p1;
p2 << iom.p2;
p3 << iom.p3;
@@

cocci.print_main("ioremap",p1)
cocci.print_secs("if",p2)
cocci.print_secs("needed iounmap",p3)

@script:python depends on report@
p1 << iom.p1;
p2 << iom.p2;
p3 << iom.p3;
@@

msg = "ERROR: missing iounmap; ioremap on line %s and execution via conditional on line %s" % (p1[0].line,p2[0].line)
coccilib.report.print_report(p3[0],msg)
