/// Find missing clk_puts.
///
//# This only signals a missing clk_put when there is a clk_put later
//# in the same function.
//# False positives can be due to loops.
//
// Confidence: Moderate
// Copyright: (C) 2012 Julia Lawall, INRIA/LIP6.  GPLv2.
// Copyright: (C) 2012 Gilles Muller, INRIA/LiP6.  GPLv2.
// URL: http://coccinelle.lip6.fr/
// Comments:
// Options:

virtual context
virtual org
virtual report

@clk@
expression e;
statement S,S1;
int ret;
position p1,p2,p3;
@@

e = clk_get@p1(...)
... when != clk_put(e)
if (<+...e...+>) S
... when any
    when != clk_put(e)
    when != if (...) { ... clk_put(e); ... }
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
   { ... when != clk_put(e)
         when forall
     return@p3 ...; }
)
... when any
clk_put(e);

@script:python depends on org@
p1 << clk.p1;
p2 << clk.p2;
p3 << clk.p3;
@@

cocci.print_main("clk_get",p1)
cocci.print_secs("if",p2)
cocci.print_secs("needed clk_put",p3)

@script:python depends on report@
p1 << clk.p1;
p2 << clk.p2;
p3 << clk.p3;
@@

msg = "ERROR: missing clk_put; clk_get on line %s and execution via conditional on line %s" % (p1[0].line,p2[0].line)
coccilib.report.print_report(p3[0],msg)
