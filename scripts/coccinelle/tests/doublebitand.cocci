/// Find bit operations that include the same argument more than once
//# One source of false positives is when the argument performs a side
//# effect.  Another source of false positives is when a neutral value
//# such as 0 for | is used to indicate no information, to maintain the
//# same structure as other similar expressions
///
// Confidence: Moderate
// Copyright: (C) 2010 Nicolas Palix, DIKU.  GPLv2.
// Copyright: (C) 2010 Julia Lawall, DIKU.  GPLv2.
// Copyright: (C) 2010 Gilles Muller, INRIA/LiP6.  GPLv2.
// URL: http://coccinelle.lip6.fr/
// Comments:
// Options: -no_includes -include_headers

virtual context
virtual org
virtual report

@r expression@
expression E;
position p;
@@

(
*        E@p
         & ... & E
|
*        E@p
         | ... | E
|
*        E@p
         & ... & !E
|
*        E@p
         | ... | !E
|
*        !E@p
         & ... & E
|
*        !E@p
         | ... | E
)

@script:python depends on org@
p << r.p;
@@

cocci.print_main("duplicated argument to & or |",p)

@script:python depends on report@
p << r.p;
@@

coccilib.report.print_report(p[0],"duplicated argument to & or |")
