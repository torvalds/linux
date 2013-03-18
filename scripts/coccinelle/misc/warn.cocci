/// Use WARN(1,...) rather than printk followed by WARN_ON(1)
///
// Confidence: High
// Copyright: (C) 2012 Julia Lawall, INRIA/LIP6.  GPLv2.
// Copyright: (C) 2012 Gilles Muller, INRIA/LiP6.  GPLv2.
// URL: http://coccinelle.lip6.fr/
// Comments:
// Options: -no_includes -include_headers

virtual patch
virtual context
virtual org
virtual report

@bad1@
position p;
@@

printk(...);
printk@p(...);
WARN_ON(1);

@r1 depends on context || report || org@
position p != bad1.p;
@@

 printk@p(...);
*WARN_ON(1);

@script:python depends on org@
p << r1.p;
@@

cocci.print_main("printk + WARN_ON can be just WARN",p)

@script:python depends on report@
p << r1.p;
@@

msg = "SUGGESTION: printk + WARN_ON can be just WARN"
coccilib.report.print_report(p[0],msg)

@ok1 depends on patch@
expression list es;
position p != bad1.p;
@@

-printk@p(
+WARN(1,
  es);
-WARN_ON(1);

@depends on patch@
expression list ok1.es;
@@

if (...)
- {
  WARN(1,es);
- }

// --------------------------------------------------------------------

@bad2@
position p;
@@

printk(...);
printk@p(...);
WARN_ON_ONCE(1);

@r2 depends on context || report || org@
position p != bad1.p;
@@

 printk@p(...);
*WARN_ON_ONCE(1);

@script:python depends on org@
p << r2.p;
@@

cocci.print_main("printk + WARN_ON_ONCE can be just WARN_ONCE",p)

@script:python depends on report@
p << r2.p;
@@

msg = "SUGGESTION: printk + WARN_ON_ONCE can be just WARN_ONCE"
coccilib.report.print_report(p[0],msg)

@ok2 depends on patch@
expression list es;
position p != bad2.p;
@@

-printk@p(
+WARN_ONCE(1,
  es);
-WARN_ON_ONCE(1);

@depends on patch@
expression list ok2.es;
@@

if (...)
- {
  WARN_ONCE(1,es);
- }
