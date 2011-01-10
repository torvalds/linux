///
/// A variable is dereference under a NULL test.
/// Even though it is know to be NULL.
///
// Confidence: Moderate
// Copyright: (C) 2010 Nicolas Palix, DIKU.  GPLv2.
// Copyright: (C) 2010 Julia Lawall, DIKU.  GPLv2.
// Copyright: (C) 2010 Gilles Muller, INRIA/LiP6.  GPLv2.
// URL: http://coccinelle.lip6.fr/
// Comments: -I ... -all_includes can give more complete results
// Options:

virtual context
virtual org
virtual report

@ifm@
expression *E;
statement S1,S2;
position p1;
@@

if@p1 ((E == NULL && ...) || ...) S1 else S2

// The following two rules are separate, because both can match a single
// expression in different ways
@pr1 expression@
expression *ifm.E;
identifier f;
position p1;
@@

 (E != NULL && ...) ? <+...E->f@p1...+> : ...

@pr2 expression@
expression *ifm.E;
identifier f;
position p2;
@@

(
  (E != NULL) && ... && <+...E->f@p2...+>
|
  (E == NULL) || ... || <+...E->f@p2...+>
|
 sizeof(<+...E->f@p2...+>)
)

// For org and report modes

@r depends on !context && (org || report) exists@
expression subE <= ifm.E;
expression *ifm.E;
expression E1,E2;
identifier f;
statement S1,S2,S3,S4;
iterator iter;
position p!={pr1.p1,pr2.p2};
position ifm.p1;
@@

if@p1 ((E == NULL && ...) || ...)
{
  ... when != if (...) S1 else S2
(
 iter(subE,...) S4 // no use
|
 list_remove_head(E2,subE,...)
|
 subE = E1
|
 for(subE = E1;...;...) S4
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
 E->f@p // bad use
)
  ... when any
  return ...;
}
else S3

@script:python depends on !context && !org && report@
p << r.p;
p1 << ifm.p1;
x << ifm.E;
@@

msg="ERROR: %s is NULL but dereferenced." % (x)
coccilib.report.print_report(p[0], msg)
cocci.include_match(False)

@script:python depends on !context && org && !report@
p << r.p;
p1 << ifm.p1;
x << ifm.E;
@@

msg="ERROR: %s is NULL but dereferenced." % (x)
msg_safe=msg.replace("[","@(").replace("]",")")
cocci.print_main(msg_safe,p)
cocci.include_match(False)

@s depends on !context && (org || report) exists@
expression subE <= ifm.E;
expression *ifm.E;
expression E1,E2;
identifier f;
statement S1,S2,S3,S4;
iterator iter;
position p!={pr1.p1,pr2.p2};
position ifm.p1;
@@

if@p1 ((E == NULL && ...) || ...)
{
  ... when != if (...) S1 else S2
(
 iter(subE,...) S4 // no use
|
 list_remove_head(E2,subE,...)
|
 subE = E1
|
 for(subE = E1;...;...) S4
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
 E->f@p // bad use
)
  ... when any
}
else S3

@script:python depends on !context && !org && report@
p << s.p;
p1 << ifm.p1;
x << ifm.E;
@@

msg="ERROR: %s is NULL but dereferenced." % (x)
coccilib.report.print_report(p[0], msg)

@script:python depends on !context && org && !report@
p << s.p;
p1 << ifm.p1;
x << ifm.E;
@@

msg="ERROR: %s is NULL but dereferenced." % (x)
msg_safe=msg.replace("[","@(").replace("]",")")
cocci.print_main(msg_safe,p)

// For context mode

@depends on context && !org && !report exists@
expression subE <= ifm.E;
expression *ifm.E;
expression E1,E2;
identifier f;
statement S1,S2,S3,S4;
iterator iter;
position p!={pr1.p1,pr2.p2};
position ifm.p1;
@@

if@p1 ((E == NULL && ...) || ...)
{
  ... when != if (...) S1 else S2
(
 iter(subE,...) S4 // no use
|
 list_remove_head(E2,subE,...)
|
 subE = E1
|
 for(subE = E1;...;...) S4
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
* E->f@p // bad use
)
  ... when any
  return ...;
}
else S3

// The following three rules are duplicates of ifm, pr1 and pr2 respectively.
// It is need because the previous rule as already made a "change".

@ifm1@
expression *E;
statement S1,S2;
position p1;
@@

if@p1 ((E == NULL && ...) || ...) S1 else S2

@pr11 expression@
expression *ifm1.E;
identifier f;
position p1;
@@

 (E != NULL && ...) ? <+...E->f@p1...+> : ...

@pr12 expression@
expression *ifm1.E;
identifier f;
position p2;
@@

(
  (E != NULL) && ... && <+...E->f@p2...+>
|
  (E == NULL) || ... || <+...E->f@p2...+>
|
 sizeof(<+...E->f@p2...+>)
)

@depends on context && !org && !report exists@
expression subE <= ifm1.E;
expression *ifm1.E;
expression E1,E2;
identifier f;
statement S1,S2,S3,S4;
iterator iter;
position p!={pr11.p1,pr12.p2};
position ifm1.p1;
@@

if@p1 ((E == NULL && ...) || ...)
{
  ... when != if (...) S1 else S2
(
 iter(subE,...) S4 // no use
|
 list_remove_head(E2,subE,...)
|
 subE = E1
|
 for(subE = E1;...;...) S4
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
* E->f@p // bad use
)
  ... when any
}
else S3
