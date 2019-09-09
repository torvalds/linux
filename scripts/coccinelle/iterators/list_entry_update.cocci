// SPDX-License-Identifier: GPL-2.0-only
/// list_for_each_entry uses its first argument to get from one element of
/// the list to the next, so it is usually not a good idea to reassign it.
/// The first rule finds such a reassignment and the second rule checks
/// that there is a path from the reassignment back to the top of the loop.
///
// Confidence: High
// Copyright: (C) 2010 Nicolas Palix, DIKU.
// Copyright: (C) 2010 Julia Lawall, DIKU.
// Copyright: (C) 2010 Gilles Muller, INRIA/LiP6.
// URL: http://coccinelle.lip6.fr/
// Comments:
// Options: --no-includes --include-headers

virtual context
virtual org
virtual report

@r exists@
iterator name list_for_each_entry;
expression x,E;
position p1,p2;
@@

list_for_each_entry@p1(x,...) { <... x =@p2 E ...> }

@depends on context && !org && !report@
expression x,E;
position r.p1,r.p2;
statement S;
@@

*x =@p2 E
...
list_for_each_entry@p1(x,...) S

// ------------------------------------------------------------------------

@back depends on (org || report) && !context exists@
expression x,E;
position r.p1,r.p2;
statement S;
@@

x =@p2 E
...
list_for_each_entry@p1(x,...) S

@script:python depends on back && org@
p1 << r.p1;
p2 << r.p2;
@@

cocci.print_main("iterator",p1)
cocci.print_secs("update",p2)

@script:python depends on back && report@
p1 << r.p1;
p2 << r.p2;
@@

msg = "iterator with update on line %s" % (p2[0].line)
coccilib.report.print_report(p1[0],msg)
