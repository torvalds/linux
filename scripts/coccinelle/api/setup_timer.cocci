/// Use setup_timer function instead of initializing timer with the function
/// and data fields
// Confidence: High
// Copyright: (C) 2016 Vaishali Thakkar, Oracle. GPLv2
// Copyright: (C) 2017 Kees Cook, Google. GPLv2
// Options: --no-includes --include-headers
// Keywords: init_timer, setup_timer

virtual patch
virtual context
virtual org
virtual report

// Match the common cases first to avoid Coccinelle parsing loops with
// "... when" clauses.

@match_immediate_function_data_after_init_timer
depends on patch && !context && !org && !report@
expression e, func, da;
@@

-init_timer
+setup_timer
 ( \(&e\|e\)
+, func, da
 );
(
-\(e.function\|e->function\) = func;
-\(e.data\|e->data\) = da;
|
-\(e.data\|e->data\) = da;
-\(e.function\|e->function\) = func;
)

@match_immediate_function_data_before_init_timer
depends on patch && !context && !org && !report@
expression e, func, da;
@@

(
-\(e.function\|e->function\) = func;
-\(e.data\|e->data\) = da;
|
-\(e.data\|e->data\) = da;
-\(e.function\|e->function\) = func;
)
-init_timer
+setup_timer
 ( \(&e\|e\)
+, func, da
 );

@match_function_and_data_after_init_timer
depends on patch && !context && !org && !report@
expression e, e2, e3, e4, e5, func, da;
@@

-init_timer
+setup_timer
 ( \(&e\|e\)
+, func, da
 );
 ... when != func = e2
     when != da = e3
(
-e.function = func;
... when != da = e4
-e.data = da;
|
-e->function = func;
... when != da = e4
-e->data = da;
|
-e.data = da;
... when != func = e5
-e.function = func;
|
-e->data = da;
... when != func = e5
-e->function = func;
)

@match_function_and_data_before_init_timer
depends on patch && !context && !org && !report@
expression e, e2, e3, e4, e5, func, da;
@@
(
-e.function = func;
... when != da = e4
-e.data = da;
|
-e->function = func;
... when != da = e4
-e->data = da;
|
-e.data = da;
... when != func = e5
-e.function = func;
|
-e->data = da;
... when != func = e5
-e->function = func;
)
... when != func = e2
    when != da = e3
-init_timer
+setup_timer
 ( \(&e\|e\)
+, func, da
 );

@r1 exists@
expression t;
identifier f;
position p;
@@

f(...) { ... when any
  init_timer@p(\(&t\|t\))
  ... when any
}

@r2 exists@
expression r1.t;
identifier g != r1.f;
expression e8;
@@

g(...) { ... when any
  \(t.data\|t->data\) = e8
  ... when any
}

// It is dangerous to use setup_timer if data field is initialized
// in another function.

@script:python depends on r2@
p << r1.p;
@@

cocci.include_match(False)

@r3 depends on patch && !context && !org && !report@
expression r1.t, func, e7;
position r1.p;
@@

(
-init_timer@p(&t);
+setup_timer(&t, func, 0UL);
... when != func = e7
-t.function = func;
|
-t.function = func;
... when != func = e7
-init_timer@p(&t);
+setup_timer(&t, func, 0UL);
|
-init_timer@p(t);
+setup_timer(t, func, 0UL);
... when != func = e7
-t->function = func;
|
-t->function = func;
... when != func = e7
-init_timer@p(t);
+setup_timer(t, func, 0UL);
)

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
