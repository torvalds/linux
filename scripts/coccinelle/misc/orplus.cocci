/// Check for constants that are added but are used elsewhere as bitmasks
/// The results should be checked manually to ensure that the nonzero
/// bits in the two constants are actually disjoint.
///
// Confidence: Moderate
// Copyright: (C) 2013 Julia Lawall, INRIA/LIP6.  GPLv2.
// Copyright: (C) 2013 Gilles Muller, INRIA/LIP6.  GPLv2.
// URL: http://coccinelle.lip6.fr/
// Comments:
// Options: --no-includes --include-headers

virtual org
virtual report
virtual context

@r@
constant c;
identifier i;
expression e;
@@

(
e | c@i
|
e & c@i
|
e |= c@i
|
e &= c@i
)

@s@
constant r.c,c1;
identifier i1;
position p;
@@

(
 c1 + c - 1
|
*c1@i1 +@p c
)

@script:python depends on org@
p << s.p;
@@

cocci.print_main("sum of probable bitmasks, consider |",p)

@script:python depends on report@
p << s.p;
@@

msg = "WARNING: sum of probable bitmasks, consider |"
coccilib.report.print_report(p[0],msg)
