/// Use setup_timer function instead of initializing timer with the function
/// and data fields
// Confidence: High
// Copyright: (C) 2016 Vaishali Thakkar, Oracle. GPLv2
// Options: --no-includes --include-headers
// Keywords: init_timer, setup_timer

virtual patch
virtual context
virtual org
virtual report

@match_immediate_function_data_after_init_timer
depends on patch && !context && !org && !report@
expression e, func, da;
@@

-init_timer (&e);
+setup_timer (&e, func, da);

(
-e.function = func;
-e.data = da;
|
-e.data = da;
-e.function = func;
)

@match_function_and_data_after_init_timer
depends on patch && !context && !org && !report@
expression e1, e2, e3, e4, e5, a, b;
@@

-init_timer (&e1);
+setup_timer (&e1, a, b);

... when != a = e2
    when != b = e3
(
-e1.function = a;
... when != b = e4
-e1.data = b;
|
-e1.data = b;
... when != a = e5
-e1.function = a;
)

@r1 exists@
identifier f;
position p;
@@

f(...) { ... when any
  init_timer@p(...)
  ... when any
}

@r2 exists@
identifier g != r1.f;
struct timer_list t;
expression e8;
@@

g(...) { ... when any
  t.data = e8
  ... when any
}

// It is dangerous to use setup_timer if data field is initialized
// in another function.

@script:python depends on r2@
p << r1.p;
@@

cocci.include_match(False)

@r3 depends on patch && !context && !org && !report@
expression e6, e7, c;
position r1.p;
@@

-init_timer@p (&e6);
+setup_timer (&e6, c, 0UL);
... when != c = e7
-e6.function = c;

// ----------------------------------------------------------------------------

@match_immediate_function_data_after_init_timer_context
depends on !patch && (context || org || report)@
expression da, e, func;
position j0, j1, j2;
@@

* init_timer@j0 (&e);
(
* e@j1.function = func;
* e@j2.data = da;
|
* e@j1.data = da;
* e@j2.function = func;
)

@match_function_and_data_after_init_timer_context
depends on !patch && (context || org || report)@
expression a, b, e1, e2, e3, e4, e5;
position j0 != match_immediate_function_data_after_init_timer_context.j0,j1,j2;
@@

* init_timer@j0 (&e1);
... when != a = e2
    when != b = e3
(
* e1@j1.function = a;
... when != b = e4
* e1@j2.data = b;
|
* e1@j1.data = b;
... when != a = e5
* e1@j2.function = a;
)

@r3_context depends on !patch && (context || org || report)@
expression c, e6, e7;
position r1.p;
position j0 !=
  {match_immediate_function_data_after_init_timer_context.j0,
   match_function_and_data_after_init_timer_context.j0}, j1;
@@

* init_timer@j0@p (&e6);
... when != c = e7
* e6@j1.function = c;

// ----------------------------------------------------------------------------

@script:python match_immediate_function_data_after_init_timer_org
depends on org@
j0 << match_immediate_function_data_after_init_timer_context.j0;
j1 << match_immediate_function_data_after_init_timer_context.j1;
j2 << match_immediate_function_data_after_init_timer_context.j2;
@@

msg = "Use setup_timer function."
coccilib.org.print_todo(j0[0], msg)
coccilib.org.print_link(j1[0], "")
coccilib.org.print_link(j2[0], "")

@script:python match_function_and_data_after_init_timer_org depends on org@
j0 << match_function_and_data_after_init_timer_context.j0;
j1 << match_function_and_data_after_init_timer_context.j1;
j2 << match_function_and_data_after_init_timer_context.j2;
@@

msg = "Use setup_timer function."
coccilib.org.print_todo(j0[0], msg)
coccilib.org.print_link(j1[0], "")
coccilib.org.print_link(j2[0], "")

@script:python r3_org depends on org@
j0 << r3_context.j0;
j1 << r3_context.j1;
@@

msg = "Use setup_timer function."
coccilib.org.print_todo(j0[0], msg)
coccilib.org.print_link(j1[0], "")

// ----------------------------------------------------------------------------

@script:python match_immediate_function_data_after_init_timer_report
depends on report@
j0 << match_immediate_function_data_after_init_timer_context.j0;
j1 << match_immediate_function_data_after_init_timer_context.j1;
@@

msg = "Use setup_timer function for function on line %s." % (j1[0].line)
coccilib.report.print_report(j0[0], msg)

@script:python match_function_and_data_after_init_timer_report depends on report@
j0 << match_function_and_data_after_init_timer_context.j0;
j1 << match_function_and_data_after_init_timer_context.j1;
@@

msg = "Use setup_timer function for function on line %s." % (j1[0].line)
coccilib.report.print_report(j0[0], msg)

@script:python r3_report depends on report@
j0 << r3_context.j0;
j1 << r3_context.j1;
@@

msg = "Use setup_timer function for function on line %s." % (j1[0].line)
coccilib.report.print_report(j0[0], msg)
