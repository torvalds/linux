/// Bool initializations should use true and false.  Bool tests don't need
/// comparisons.  Based on contributions from Joe Perches, Rusty Russell
/// and Bruce W Allan.
///
// Confidence: High
// Copyright: (C) 2012 Julia Lawall, INRIA/LIP6.  GPLv2.
// Copyright: (C) 2012 Gilles Muller, INRIA/LiP6.  GPLv2.
// URL: http://coccinelle.lip6.fr/
// Options: --include-headers

virtual patch
virtual context
virtual org
virtual report

@boolok@
symbol true,false;
@@
(
true
|
false
)

@depends on patch@
bool t;
@@

(
- t == true
+ t
|
- true == t
+ t
|
- t != true
+ !t
|
- true != t
+ !t
|
- t == false
+ !t
|
- false == t
+ !t
|
- t != false
+ t
|
- false != t
+ t
)

@depends on patch disable is_zero, isnt_zero@
bool t;
@@

(
- t == 1
+ t
|
- t != 1
+ !t
|
- t == 0
+ !t
|
- t != 0
+ t
)

@depends on patch && boolok@
bool b;
@@
(
 b =
- 0
+ false
|
 b =
- 1
+ true
)

// ---------------------------------------------------------------------

@r1 depends on !patch@
bool t;
position p;
@@

(
* t@p == true
|
* true == t@p
|
* t@p != true
|
* true != t@p
|
* t@p == false
|
* false == t@p
|
* t@p != false
|
* false != t@p
)

@r2 depends on !patch disable is_zero, isnt_zero@
bool t;
position p;
@@

(
* t@p == 1
|
* t@p != 1
|
* t@p == 0
|
* t@p != 0
)

@r3 depends on !patch && boolok@
bool b;
position p1;
@@
(
*b@p1 = 0
|
*b@p1 = 1
)

@r4 depends on !patch@
bool b;
position p2;
identifier i;
constant c != {0,1};
@@
(
 b = i
|
*b@p2 = c
)

@script:python depends on org@
p << r1.p;
@@

cocci.print_main("WARNING: Comparison to bool",p)

@script:python depends on org@
p << r2.p;
@@

cocci.print_main("WARNING: Comparison of 0/1 to bool variable",p)

@script:python depends on org@
p1 << r3.p1;
@@

cocci.print_main("WARNING: Assignment of 0/1 to bool variable",p1)

@script:python depends on org@
p2 << r4.p2;
@@

cocci.print_main("ERROR: Assignment of non-0/1 constant to bool variable",p2)

@script:python depends on report@
p << r1.p;
@@

coccilib.report.print_report(p[0],"WARNING: Comparison to bool")

@script:python depends on report@
p << r2.p;
@@

coccilib.report.print_report(p[0],"WARNING: Comparison of 0/1 to bool variable")

@script:python depends on report@
p1 << r3.p1;
@@

coccilib.report.print_report(p1[0],"WARNING: Assignment of 0/1 to bool variable")

@script:python depends on report@
p2 << r4.p2;
@@

coccilib.report.print_report(p2[0],"ERROR: Assignment of non-0/1 constant to bool variable")
