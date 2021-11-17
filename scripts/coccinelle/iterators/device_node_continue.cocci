// SPDX-License-Identifier: GPL-2.0-only
/// Device node iterators put the previous value of the index variable, so an
/// explicit put causes a double put.
///
// Confidence: High
// Copyright: (C) 2015 Julia Lawall, Inria.
// URL: http://coccinelle.lip6.fr/
// Options: --no-includes --include-headers
// Requires: 1.0.4
// Keywords: for_each_child_of_node, etc.

// This uses a conjunction, which requires at least coccinelle >= 1.0.4

virtual patch
virtual context
virtual org
virtual report

@r exists@
expression e1,e2;
local idexpression n;
iterator name for_each_node_by_name, for_each_node_by_type,
for_each_compatible_node, for_each_matching_node,
for_each_matching_node_and_match, for_each_child_of_node,
for_each_available_child_of_node, for_each_node_with_property;
iterator i;
position p1,p2;
statement S;
@@

(
(
for_each_node_by_name(n,e1) S
|
for_each_node_by_type(n,e1) S
|
for_each_compatible_node(n,e1,e2) S
|
for_each_matching_node(n,e1) S
|
for_each_matching_node_and_match(n,e1,e2) S
|
for_each_child_of_node(e1,n) S
|
for_each_available_child_of_node(e1,n) S
|
for_each_node_with_property(n,e1) S
)
&
i@p1(...) {
   ... when != of_node_get(n)
       when any
   of_node_put@p2(n);
   ... when any
}
)

@s exists@
local idexpression r.n;
statement S;
position r.p1,r.p2;
iterator i;
@@

 of_node_put@p2(n);
 ... when any
 i@p1(..., n, ...)
 S

@t depends on s && patch && !context && !org && !report@
local idexpression n;
position r.p2;
@@

- of_node_put@p2(n);

// ----------------------------------------------------------------------------

@t_context depends on s && !patch && (context || org || report)@
local idexpression n;
position r.p2;
position j0;
@@

*  of_node_put@j0@p2(n);

// ----------------------------------------------------------------------------

@script:python t_org depends on org@
j0 << t_context.j0;
@@

msg = "ERROR: probable double put."
coccilib.org.print_todo(j0[0], msg)

// ----------------------------------------------------------------------------

@script:python t_report depends on report@
j0 << t_context.j0;
@@

msg = "ERROR: probable double put."
coccilib.report.print_report(j0[0], msg)

