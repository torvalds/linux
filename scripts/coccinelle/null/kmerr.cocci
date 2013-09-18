/// This semantic patch looks for kmalloc etc that are not followed by a
/// NULL check.  It only gives a report in the case where there is some
/// error handling code later in the function, which may be helpful
/// in determining what the error handling code for the call to kmalloc etc
/// should be.
///
// Confidence: High
// Copyright: (C) 2010 Nicolas Palix, DIKU.  GPLv2.
// Copyright: (C) 2010 Julia Lawall, DIKU.  GPLv2.
// Copyright: (C) 2010 Gilles Muller, INRIA/LiP6.  GPLv2.
// URL: http://coccinelle.lip6.fr/
// Comments:
// Options: --no-includes --include-headers

virtual context
virtual org
virtual report

@withtest@
expression x;
position p;
identifier f,fld;
@@

x@p = f(...);
... when != x->fld
\(x == NULL \| x != NULL\)

@fixed depends on context && !org && !report@
expression x,x1;
position p1 != withtest.p;
statement S;
position any withtest.p;
identifier f;
@@

*x@p1 = \(kmalloc\|kzalloc\|kcalloc\)(...);
...
*x1@p = f(...);
if (!x1) S

// ------------------------------------------------------------------------

@rfixed depends on (org || report) && !context exists@
expression x,x1;
position p1 != withtest.p;
position p2;
statement S;
position any withtest.p;
identifier f;
@@

x@p1 = \(kmalloc\|kzalloc\|kcalloc\)(...);
...
x1@p = f@p2(...);
if (!x1) S

@script:python depends on org@
p1 << rfixed.p1;
p2 << rfixed.p2;
@@

cocci.print_main("alloc call",p1)
cocci.print_secs("possible model",p2)

@script:python depends on report@
p1 << rfixed.p1;
p2 << rfixed.p2;
@@

msg = "alloc with no test, possible model on line %s" % (p2[0].line)
coccilib.report.print_report(p1[0],msg)
