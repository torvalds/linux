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
-	((E > 1) ? "s" : "")
+	str_plural(E)
)

@str_plural_r depends on !patch@
expression E;
position P;
@@
(
*	(E@P == 1) ? "" : "s"
|
*	(E@P > 1) ? "s" : ""
)

@script:python depends on report@
p << str_plural_r.P;
e << str_plural_r.E;
@@

coccilib.report.print_report(p[0], "opportunity for str_plural(%s)" % e)

@str_up_down depends on patch disable neg_if_exp@
expression E;
@@
-	((E) ? "up" : "down")
+	str_up_down(E)

@str_up_down_r depends on !patch disable neg_if_exp@
expression E;
position P;
@@
*	E@P ? "up" : "down"

@script:python depends on report@
p << str_up_down_r.P;
e << str_up_down_r.E;
@@

coccilib.report.print_report(p[0], "opportunity for str_up_down(%s)" % e)

@str_down_up depends on patch disable neg_if_exp@
expression E;
@@
-      ((E) ? "down" : "up")
+      str_down_up(E)

@str_down_up_r depends on !patch disable neg_if_exp@
expression E;
position P;
@@
*      E@P ? "down" : "up"

@script:python depends on report@
p << str_down_up_r.P;
e << str_down_up_r.E;
@@

coccilib.report.print_report(p[0], "opportunity for str_down_up(%s)" % e)

@str_true_false depends on patch disable neg_if_exp@
expression E;
@@
-      ((E) ? "true" : "false")
+      str_true_false(E)

@str_true_false_r depends on !patch disable neg_if_exp@
expression E;
position P;
@@
*      E@P ? "true" : "false"

@script:python depends on report@
p << str_true_false_r.P;
e << str_true_false_r.E;
@@

coccilib.report.print_report(p[0], "opportunity for str_true_false(%s)" % e)

@str_false_true depends on patch disable neg_if_exp@
expression E;
@@
-      ((E) ? "false" : "true")
+      str_false_true(E)

@str_false_true_r depends on !patch disable neg_if_exp@
expression E;
position P;
@@
*      E@P ? "false" : "true"

@script:python depends on report@
p << str_false_true_r.P;
e << str_false_true_r.E;
@@

coccilib.report.print_report(p[0], "opportunity for str_false_true(%s)" % e)

@str_hi_lo depends on patch disable neg_if_exp@
expression E;
@@
-      ((E) ? "hi" : "lo")
+      str_hi_lo(E)

@str_hi_lo_r depends on !patch disable neg_if_exp@
expression E;
position P;
@@
*      E@P ? "hi" : "lo"

@script:python depends on report@
p << str_hi_lo_r.P;
e << str_hi_lo_r.E;
@@

coccilib.report.print_report(p[0], "opportunity for str_hi_lo(%s)" % e)

@str_high_low depends on patch disable neg_if_exp@
expression E;
@@
-      ((E) ? "high" : "low")
+      str_high_low(E)

@str_high_low_r depends on !patch disable neg_if_exp@
expression E;
position P;
@@
*      E@P ? "high" : "low"

@script:python depends on report@
p << str_high_low_r.P;
e << str_high_low_r.E;
@@

coccilib.report.print_report(p[0], "opportunity for str_high_low(%s)" % e)

@str_lo_hi depends on patch disable neg_if_exp@
expression E;
@@
-      ((E) ? "lo" : "hi")
+      str_lo_hi(E)

@str_lo_hi_r depends on !patch disable neg_if_exp@
expression E;
position P;
@@
*      E@P ? "lo" : "hi"

@script:python depends on report@
p << str_lo_hi_r.P;
e << str_lo_hi_r.E;
@@

coccilib.report.print_report(p[0], "opportunity for str_lo_hi(%s)" % e)

@str_low_high depends on patch disable neg_if_exp@
expression E;
@@
-      ((E) ? "low" : "high")
+      str_low_high(E)

@str_low_high_r depends on !patch disable neg_if_exp@
expression E;
position P;
@@
*      E@P ? "low" : "high"

@script:python depends on report@
p << str_low_high_r.P;
e << str_low_high_r.E;
@@

coccilib.report.print_report(p[0], "opportunity for str_low_high(%s)" % e)

@str_enable_disable depends on patch@
expression E;
@@
-      ((E) ? "enable" : "disable")
+      str_enable_disable(E)

@str_enable_disable_r depends on !patch@
expression E;
position P;
@@
*      E@P ? "enable" : "disable"

@script:python depends on report@
p << str_enable_disable_r.P;
e << str_enable_disable_r.E;
@@

coccilib.report.print_report(p[0], "opportunity for str_enable_disable(%s)" % e)

@str_enabled_disabled depends on patch@
expression E;
@@
-      ((E) ? "enabled" : "disabled")
+      str_enabled_disabled(E)

@str_enabled_disabled_r depends on !patch@
expression E;
position P;
@@
*      E@P ? "enabled" : "disabled"

@script:python depends on report@
p << str_enabled_disabled_r.P;
e << str_enabled_disabled_r.E;
@@

coccilib.report.print_report(p[0], "opportunity for str_enabled_disabled(%s)" % e)

@str_read_write depends on patch disable neg_if_exp@
expression E;
@@
-      ((E) ? "read" : "write")
+      str_read_write(E)

@str_read_write_r depends on !patch disable neg_if_exp@
expression E;
position P;
@@
*      E@P ? "read" : "write"

@script:python depends on report@
p << str_read_write_r.P;
e << str_read_write_r.E;
@@

coccilib.report.print_report(p[0], "opportunity for str_read_write(%s)" % e)

@str_write_read depends on patch disable neg_if_exp@
expression E;
@@
-      ((E) ? "write" : "read")
+      str_write_read(E)

@str_write_read_r depends on !patch disable neg_if_exp@
expression E;
position P;
@@
*      E@P ? "write" : "read"

@script:python depends on report@
p << str_write_read_r.P;
e << str_write_read_r.E;
@@

coccilib.report.print_report(p[0], "opportunity for str_write_read(%s)" % e)

@str_on_off depends on patch@
expression E;
@@
-      ((E) ? "on" : "off")
+      str_on_off(E)

@str_on_off_r depends on !patch@
expression E;
position P;
@@
*      E@P ? "on" : "off"

@script:python depends on report@
p << str_on_off_r.P;
e << str_on_off_r.E;
@@

coccilib.report.print_report(p[0], "opportunity for str_on_off(%s)" % e)

@str_yes_no depends on patch@
expression E;
@@
-      ((E) ? "yes" : "no")
+      str_yes_no(E)

@str_yes_no_r depends on !patch@
expression E;
position P;
@@
*      E@P ? "yes" : "no"

@script:python depends on report@
p << str_yes_no_r.P;
e << str_yes_no_r.E;
@@

coccilib.report.print_report(p[0], "opportunity for str_yes_no(%s)" % e)
