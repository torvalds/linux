// SPDX-License-Identifier: GPL-2.0-only
/// Find places to use string_choices.h's various helpers.
//
// Confidence: Medium
// Options: --no-includes --include-headers
virtual patch
virtual context
virtual report

@str_plural depends on patch@
expression E;
@@
(
-	((E == 1) ? "" : "s")
+	str_plural(E)
|
-	((E != 1) ? "s" : "")
+	str_plural(E)
|
-	((E > 1) ? "s" : "")
+	str_plural(E)
)

@str_plural_r depends on !patch exists@
expression E;
position P;
@@
(
*	((E@P == 1) ? "" : "s")
|
*	((E@P != 1) ? "s" : "")
|
*	((E@P > 1) ? "s" : "")
)

@script:python depends on report@
p << str_plural_r.P;
e << str_plural_r.E;
@@

coccilib.report.print_report(p[0], "opportunity for str_plural(%s)" % e)
