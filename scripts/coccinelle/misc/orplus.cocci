// SPDX-License-Identifier: GPL-2.0-only
/// Check for constants that are added but are used elsewhere as bitmasks
/// The results should be checked manually to ensure that the nonzero
/// bits in the two constants are actually disjoint.
///
// Confidence: Moderate
// Copyright: (C) 2013 Julia Lawall, INRIA/LIP6.
// Copyright: (C) 2013 Gilles Muller, INRIA/LIP6.
// URL: https://coccinelle.gitlabpages.inria.fr/website
// Comments:
// Options: --no-includes --include-headers

virtual org
virtual report
virtual context

@r@
constant c,c1;
identifier i,i1;
position p;
@@

(
 c1 + c - 1
|
 c1@i1 +@p c@i
)

@s@
constant r.c, r.c1;
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
|
e | c1@i
|
e & c1@i
|
e |= c1@i
|
e &= c1@i
)

@depends on s@
position r.p;
constant c1,c2;
@@

* c1 +@p c2

@script:python depends on s && org@
p << r.p;
@@

cocci.print_main("sum of probable bitmasks, consider |",p)

@script:python depends on s && report@
p << r.p;
@@

msg = "WARNING: sum of probable bitmasks, consider |"
coccilib.report.print_report(p[0],msg)
