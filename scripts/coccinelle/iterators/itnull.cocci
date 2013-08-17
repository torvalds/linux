/// Many iterators have the property that the first argument is always bound
/// to a real list element, never NULL.
//# False positives arise for some iterators that do not have this property,
//# or in cases when the loop cursor is reassigned.  The latter should only
//# happen when the matched code is on the way to a loop exit (break, goto,
//# or return).
///
// Confidence: Moderate
// Copyright: (C) 2010-2012 Nicolas Palix.  GPLv2.
// Copyright: (C) 2010-2012 Julia Lawall, INRIA/LIP6.  GPLv2.
// Copyright: (C) 2010-2012 Gilles Muller, INRIA/LiP6.  GPLv2.
// URL: http://coccinelle.lip6.fr/
// Comments:
// Options: -no_includes -include_headers

virtual patch
virtual context
virtual org
virtual report

@depends on patch@
iterator I;
expression x,E,E1,E2;
statement S,S1,S2;
@@

I(x,...) { <...
(
- if (x == NULL && ...) S
|
- if (x != NULL || ...)
  S
|
- (x == NULL) ||
  E
|
- (x != NULL) &&
  E
|
- (x == NULL && ...) ? E1 :
  E2
|
- (x != NULL || ...) ?
  E1
- : E2
|
- if (x == NULL && ...) S1 else
  S2
|
- if (x != NULL || ...)
  S1
- else S2
|
+ BAD(
  x == NULL
+ )
|
+ BAD(
  x != NULL
+ )
)
  ...> }

@r depends on !patch exists@
iterator I;
expression x,E;
position p1,p2;
@@

*I@p1(x,...)
{ ... when != x = E
(
*  x@p2 == NULL
|
*  x@p2 != NULL
)
  ... when any
}

@script:python depends on org@
p1 << r.p1;
p2 << r.p2;
@@

cocci.print_main("iterator-bound variable",p1)
cocci.print_secs("useless NULL test",p2)

@script:python depends on report@
p1 << r.p1;
p2 << r.p2;
@@

msg = "ERROR: iterator variable bound on line %s cannot be NULL" % (p1[0].line)
coccilib.report.print_report(p2[0], msg)
