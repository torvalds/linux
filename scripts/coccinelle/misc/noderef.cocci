/// sizeof when applied to a pointer typed expression gives the size of
/// the pointer
///
// Confidence: High
// Copyright: (C) 2012 Julia Lawall, INRIA/LIP6.  GPLv2.
// Copyright: (C) 2012 Gilles Muller, INRIA/LiP6.  GPLv2.
// URL: http://coccinelle.lip6.fr/
// Comments:
// Options: --no-includes --include-headers

virtual org
virtual report
virtual context
virtual patch

@depends on patch@
expression *x;
expression f;
expression i;
type T;
@@

(
x = <+... sizeof(
- x
+ *x
   ) ...+>
|
f(...,(T)(x),...,sizeof(
- x
+ *x
   ),...)
|
f(...,sizeof(
- x
+ *x
   ),...,(T)(x),...)
|
f(...,(T)(x),...,i*sizeof(
- x
+ *x
   ),...)
|
f(...,i*sizeof(
- x
+ *x
   ),...,(T)(x),...)
)

@r depends on !patch@
expression *x;
expression f;
expression i;
position p;
type T;
@@

(
*x = <+... sizeof@p(x) ...+>
|
*f(...,(T)(x),...,sizeof@p(x),...)
|
*f(...,sizeof@p(x),...,(T)(x),...)
|
*f(...,(T)(x),...,i*sizeof@p(x),...)
|
*f(...,i*sizeof@p(x),...,(T)(x),...)
)

@script:python depends on org@
p << r.p;
@@

cocci.print_main("application of sizeof to pointer",p)

@script:python depends on report@
p << r.p;
@@

msg = "ERROR: application of sizeof to pointer"
coccilib.report.print_report(p[0],msg)
