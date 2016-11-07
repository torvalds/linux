/// Free of a structure field
///
// Confidence: High
// Copyright: (C) 2013 Julia Lawall, INRIA/LIP6.  GPLv2.
// URL: http://coccinelle.lip6.fr/
// Comments:
// Options: --no-includes --include-headers

virtual org
virtual report
virtual context

@r depends on context || report || org @
expression e;
identifier f;
position p;
@@

(
* kfree@p(&e->f)
|
* kzfree@p(&e->f)
)

@script:python depends on org@
p << r.p;
@@

cocci.print_main("kfree",p)

@script:python depends on report@
p << r.p;
@@

msg = "ERROR: invalid free of structure field"
coccilib.report.print_report(p[0],msg)
