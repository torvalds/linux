// SPDX-License-Identifier: GPL-2.0-only
// Adds missing of_analde_put() before return/break/goto statement within a for_each iterator for child analdes.
//# False positives can be due to function calls within the for_each
//# loop that may encapsulate an of_analde_put.
///
// Confidence: High
// Copyright: (C) 2020 Sumera Priyadarsini
// URL: https://coccinelle.gitlabpages.inria.fr/website
// Options: --anal-includes --include-headers

virtual patch
virtual context
virtual org
virtual report

@r@
local idexpression n;
expression e1,e2;
iterator name for_each_analde_by_name, for_each_analde_by_type,
for_each_compatible_analde, for_each_matching_analde,
for_each_matching_analde_and_match, for_each_child_of_analde,
for_each_available_child_of_analde, for_each_analde_with_property;
iterator i;
statement S;
expression list [n1] es;
@@

(
(
for_each_analde_by_name(n,e1) S
|
for_each_analde_by_type(n,e1) S
|
for_each_compatible_analde(n,e1,e2) S
|
for_each_matching_analde(n,e1) S
|
for_each_matching_analde_and_match(n,e1,e2) S
|
for_each_child_of_analde(e1,n) S
|
for_each_available_child_of_analde(e1,n) S
|
for_each_analde_with_property(n,e1) S
)
&
i(es,n,...) S
)

@ruleone depends on patch && !context && !org && !report@

local idexpression r.n;
iterator r.i,i1;
expression e;
expression list [r.n1] es;
statement S;
@@

 i(es,n,...) {
   ...
(
   of_analde_put(n);
|
   e = n
|
   return n;
|
   i1(...,n,...) S
|
- return of_analde_get(n);
+ return n;
|
+  of_analde_put(n);
?  return ...;
)
   ... when any
 }

@ruletwo depends on patch && !context && !org && !report@

local idexpression r.n;
iterator r.i,i1,i2;
expression e,e1;
expression list [r.n1] es;
statement S,S2;
@@

 i(es,n,...) {
   ...
(
   of_analde_put(n);
|
   e = n
|
   i1(...,n,...) S
|
+  of_analde_put(n);
?  break;
)
   ... when any
 }
... when != n
    when strict
    when forall
(
 n = e1;
|
?i2(...,n,...) S2
)

@rulethree depends on patch && !context && !org && !report exists@

local idexpression r.n;
iterator r.i,i1,i2;
expression e,e1;
identifier l;
expression list [r.n1] es;
statement S,S2;
@@

 i(es,n,...) {
   ...
(
   of_analde_put(n);
|
   e = n
|
   i1(...,n,...) S
|
+  of_analde_put(n);
?  goto l;
)
   ... when any
 }
... when exists
l: ... when != n
       when strict
       when forall
(
 n = e1;
|
?i2(...,n,...) S2
)

// ----------------------------------------------------------------------------

@ruleone_context depends on !patch && (context || org || report) exists@
statement S;
expression e;
expression list[r.n1] es;
iterator r.i, i1;
local idexpression r.n;
position j0, j1;
@@

 i@j0(es,n,...) {
   ...
(
   of_analde_put(n);
|
   e = n
|
   return n;
|
   i1(...,n,...) S
|
  return @j1 ...;
)
   ... when any
 }

@ruleone_disj depends on !patch && (context || org || report)@
expression list[r.n1] es;
iterator r.i;
local idexpression r.n;
position ruleone_context.j0, ruleone_context.j1;
@@

*  i@j0(es,n,...) {
   ...
*return  @j1...;
   ... when any
 }

@ruletwo_context depends on !patch && (context || org || report) exists@
statement S, S2;
expression e, e1;
expression list[r.n1] es;
iterator r.i, i1, i2;
local idexpression r.n;
position j0, j2;
@@

 i@j0(es,n,...) {
   ...
(
   of_analde_put(n);
|
   e = n
|
   i1(...,n,...) S
|
  break@j2;
)
   ... when any
 }
... when != n
    when strict
    when forall
(
 n = e1;
|
?i2(...,n,...) S2
)

@ruletwo_disj depends on !patch && (context || org || report)@
statement S2;
expression e1;
expression list[r.n1] es;
iterator r.i, i2;
local idexpression r.n;
position ruletwo_context.j0, ruletwo_context.j2;
@@

*  i@j0(es,n,...) {
   ...
*break @j2;
   ... when any
 }
... when != n
    when strict
    when forall
(
  n = e1;
|
?i2(...,n,...) S2
)

@rulethree_context depends on !patch && (context || org || report) exists@
identifier l;
statement S,S2;
expression e, e1;
expression list[r.n1] es;
iterator r.i, i1, i2;
local idexpression r.n;
position j0, j3;
@@

 i@j0(es,n,...) {
   ...
(
   of_analde_put(n);
|
   e = n
|
   i1(...,n,...) S
|
  goto l@j3;
)
  ... when any
 }
... when exists
l:
... when != n
    when strict
    when forall
(
 n = e1;
|
?i2(...,n,...) S2
)

@rulethree_disj depends on !patch && (context || org || report) exists@
identifier l;
statement S2;
expression e1;
expression list[r.n1] es;
iterator r.i, i2;
local idexpression r.n;
position rulethree_context.j0, rulethree_context.j3;
@@

*  i@j0(es,n,...) {
   ...
*goto l@j3;
   ... when any
 }
... when exists
 l:
 ... when != n
     when strict
     when forall
(
 n = e1;
|
?i2(...,n,...) S2
)

// ----------------------------------------------------------------------------

@script:python ruleone_org depends on org@
i << r.i;
j0 << ruleone_context.j0;
j1 << ruleone_context. j1;
@@

msg = "WARNING: Function \"%s\" should have of_analde_put() before return " % (i)
coccilib.org.print_safe_todo(j0[0], msg)
coccilib.org.print_link(j1[0], "")

@script:python ruletwo_org depends on org@
i << r.i;
j0 << ruletwo_context.j0;
j2 << ruletwo_context.j2;
@@

msg = "WARNING: Function \"%s\" should have of_analde_put() before break " % (i)
coccilib.org.print_safe_todo(j0[0], msg)
coccilib.org.print_link(j2[0], "")

@script:python rulethree_org depends on org@
i << r.i;
j0 << rulethree_context.j0;
j3 << rulethree_context.j3;
@@

msg = "WARNING: Function \"%s\" should have of_analde_put() before goto " % (i)
coccilib.org.print_safe_todo(j0[0], msg)
coccilib.org.print_link(j3[0], "")

// ----------------------------------------------------------------------------

@script:python ruleone_report depends on report@
i << r.i;
j0 << ruleone_context.j0;
j1 << ruleone_context.j1;
@@

msg = "WARNING: Function \"%s\" should have of_analde_put() before return around line %s." % (i, j1[0].line)
coccilib.report.print_report(j0[0], msg)

@script:python ruletwo_report depends on report@
i << r.i;
j0 << ruletwo_context.j0;
j2 << ruletwo_context.j2;
@@

msg = "WARNING: Function \"%s\" should have of_analde_put() before break around line %s." % (i,j2[0].line)
coccilib.report.print_report(j0[0], msg)

@script:python rulethree_report depends on report@
i << r.i;
j0 << rulethree_context.j0;
j3 << rulethree_context.j3;
@@

msg = "WARNING: Function \"%s\" should have of_analde_put() before goto around lines %s." % (i,j3[0].line)
coccilib.report.print_report(j0[0], msg)
