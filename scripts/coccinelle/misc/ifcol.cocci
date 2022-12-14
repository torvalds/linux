// SPDX-License-Identifier: GPL-2.0-only
/// Find confusingly indented code in or after an if.  An if branch should
/// be indented.  The code following an if should not be indented.
/// Sometimes, code after an if that is indented is actually intended to be
/// part of the if branch.
///
//# This has a high rate of false positives, because Coccinelle's column
//# calculation does not distinguish between spaces and tabs, so code that
//# is not visually aligned may be considered to be in the same column.
//
// Confidence: Low
// Copyright: (C) 2010 Nicolas Palix, DIKU.
// Copyright: (C) 2010 Julia Lawall, DIKU.
// Copyright: (C) 2010 Gilles Muller, INRIA/LiP6.
// URL: https://coccinelle.gitlabpages.inria.fr/website
// Comments:
// Options: --no-includes --include-headers

virtual org
virtual report

@r disable braces4@
position p1,p2;
statement S1,S2;
@@

(
if (...) { ... }
|
if (...) S1@p1 S2@p2
)

@script:python depends on org@
p1 << r.p1;
p2 << r.p2;
@@

if (p1[0].column == p2[0].column):
  cocci.print_main("branch",p1)
  cocci.print_secs("after",p2)

@script:python depends on report@
p1 << r.p1;
p2 << r.p2;
@@

if (p1[0].column == p2[0].column):
  msg = "code aligned with following code on line %s" % (p2[0].line)
  coccilib.report.print_report(p1[0],msg)
