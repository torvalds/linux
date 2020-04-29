// SPDX-License-Identifier: GPL-2.0-only
/// Find a use after free.
//# Values of variables may imply that some
//# execution paths are not possible, resulting in false positives.
//# Another source of false positives are macros such as
//# SCTP_DBG_OBJCNT_DEC that do not actually evaluate their argument
///
// Confidence: Moderate
// Copyright: (C) 2010-2012 Nicolas Palix.
// Copyright: (C) 2010-2012 Julia Lawall, INRIA/LIP6.
// Copyright: (C) 2010-2012 Gilles Muller, INRIA/LiP6.
// URL: http://coccinelle.lip6.fr/
// Comments:
// Options: --no-includes --include-headers

virtual org
virtual report

@free@
expression E;
position p1;
@@

(
* kfree@p1(E)
|
* kzfree@p1(E)
)

@print expression@
constant char [] c;
expression free.E,E2;
type T;
position p;
identifier f;
@@

(
 f(...,c,...,(T)E@p,...)
|
 E@p == E2
|
 E@p != E2
|
 E2 == E@p
|
 E2 != E@p
|
 !E@p
|
 E@p || ...
)

@sz@
expression free.E;
position p;
@@

 sizeof(<+...E@p...+>)

@loop exists@
expression E;
identifier l;
position ok;
@@

while (1) { ...
(
* kfree@ok(E)
|
* kzfree@ok(E)
)
  ... when != break;
      when != goto l;
      when forall
}

@r exists@
expression free.E, subE<=free.E, E2;
expression E1;
iterator iter;
statement S;
position free.p1!=loop.ok,p2!={print.p,sz.p};
@@

(
* kfree@p1(E,...)
|
* kzfree@p1(E,...)
)
...
(
 iter(...,subE,...) S // no use
|
 list_remove_head(E1,subE,...)
|
 subE = E2
|
 subE++
|
 ++subE
|
 --subE
|
 subE--
|
 &subE
|
 BUG(...)
|
 BUG_ON(...)
|
 return_VALUE(...)
|
 return_ACPI_STATUS(...)
|
 E@p2 // bad use
)

@script:python depends on org@
p1 << free.p1;
p2 << r.p2;
@@

cocci.print_main("kfree",p1)
cocci.print_secs("ref",p2)

@script:python depends on report@
p1 << free.p1;
p2 << r.p2;
@@

msg = "ERROR: reference preceded by free on line %s" % (p1[0].line)
coccilib.report.print_report(p2[0],msg)
