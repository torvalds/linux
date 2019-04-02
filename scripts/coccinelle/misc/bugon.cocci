/// Use _ON instead of a if condition followed by .
///
//# This makes an effort to find cases where () follows an if
//# condition on an expression and replaces the if condition and ()
//# with a _ON having the conditional expression of the if statement
//# as argument.
//
// Confidence: High
// Copyright: (C) 2014 Himangi Saraogi.  GPLv2.
// Comments:
// Options: --no-includes --include-headers

virtual patch
virtual context
virtual org
virtual report

//----------------------------------------------------------
//  For context mode
//----------------------------------------------------------

@depends on context@
expression e;
@@

*if (e) ();

//----------------------------------------------------------
//  For patch mode
//----------------------------------------------------------

@depends on patch@
expression e;
@@

-if (e) ();
+_ON(e);

//----------------------------------------------------------
//  For org and report mode
//----------------------------------------------------------

@r depends on (org || report)@
expression e;
position p;
@@

 if (e) @p ();

@script:python depends on org@
p << r.p;
@@

coccilib.org.print_todo(p[0], "WARNING use _ON")

@script:python depends on report@
p << r.p;
@@

msg="WARNING: Use _ON instead of if condition followed by .\nPlease make sure the condition has no side effects (see conditional _ON definition in include/asm-generic/.h)"
coccilib.report.print_report(p[0], msg)

