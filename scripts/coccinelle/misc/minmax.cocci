// SPDX-License-Identifier: GPL-2.0-only
///
/// Check for opencoded min(), max() implementations.
/// Generated patches sometimes require adding a cast to fix compile warning.
/// Warnings/patches scope intentionally limited to a function body.
///
// Confidence: Medium
// Copyright: (C) 2021 Denis Efremov ISPRAS
// Options: --no-includes --include-headers
//
// Keywords: min, max
//


virtual report
virtual org
virtual context
virtual patch

@rmax depends on !patch@
identifier func;
expression x, y;
binary operator cmp = {>, >=};
position p;
@@

func(...)
{
	<...
*	((x) cmp@p (y) ? (x) : (y))
	...>
}

@rmaxif depends on !patch@
identifier func;
expression x, y;
expression max_val;
binary operator cmp = {>, >=};
position p;
@@

func(...)
{
	<...
*	if ((x) cmp@p (y)) {
*		max_val = (x);
*	} else {
*		max_val = (y);
*	}
	...>
}

@rmin depends on !patch@
identifier func;
expression x, y;
binary operator cmp = {<, <=};
position p;
@@

func(...)
{
	<...
*	((x) cmp@p (y) ? (x) : (y))
	...>
}

@rminif depends on !patch@
identifier func;
expression x, y;
expression min_val;
binary operator cmp = {<, <=};
position p;
@@

func(...)
{
	<...
*	if ((x) cmp@p (y)) {
*		min_val = (x);
*	} else {
*		min_val = (y);
*	}
	...>
}

@pmax depends on patch@
identifier func;
expression x, y;
binary operator cmp = {>=, >};
@@

func(...)
{
	<...
-	((x) cmp (y) ? (x) : (y))
+	max(x, y)
	...>
}

@pmaxif depends on patch@
identifier func;
expression x, y;
expression max_val;
binary operator cmp = {>=, >};
@@

func(...)
{
	<...
-	if ((x) cmp (y)) {
-		max_val = x;
-	} else {
-		max_val = y;
-	}
+	max_val = max(x, y);
	...>
}

// Don't generate patches for errcode returns.
@errcode depends on patch@
position p;
identifier func;
expression x;
binary operator cmp = {<, <=};
@@

func(...)
{
	<...
	return ((x) cmp@p 0 ? (x) : 0);
	...>
}

@pmin depends on patch@
identifier func;
expression x, y;
binary operator cmp = {<=, <};
position p != errcode.p;
@@

func(...)
{
	<...
-	((x) cmp@p (y) ? (x) : (y))
+	min(x, y)
	...>
}

@pminif depends on patch@
identifier func;
expression x, y;
expression min_val;
binary operator cmp = {<=, <};
@@

func(...)
{
	<...
-	if ((x) cmp (y)) {
-		min_val = x;
-	} else {
-		min_val = y;
-	}
+	min_val = min(x, y);
	...>
}

@script:python depends on report@
p << rmax.p;
@@

for p0 in p:
	coccilib.report.print_report(p0, "WARNING opportunity for max()")

@script:python depends on org@
p << rmax.p;
@@

for p0 in p:
	coccilib.org.print_todo(p0, "WARNING opportunity for max()")

@script:python depends on report@
p << rmaxif.p;
@@

for p0 in p:
	coccilib.report.print_report(p0, "WARNING opportunity for max()")

@script:python depends on org@
p << rmaxif.p;
@@

for p0 in p:
	coccilib.org.print_todo(p0, "WARNING opportunity for max()")

@script:python depends on report@
p << rmin.p;
@@

for p0 in p:
	coccilib.report.print_report(p0, "WARNING opportunity for min()")

@script:python depends on org@
p << rmin.p;
@@

for p0 in p:
	coccilib.org.print_todo(p0, "WARNING opportunity for min()")

@script:python depends on report@
p << rminif.p;
@@

for p0 in p:
	coccilib.report.print_report(p0, "WARNING opportunity for min()")

@script:python depends on org@
p << rminif.p;
@@

for p0 in p:
	coccilib.org.print_todo(p0, "WARNING opportunity for min()")
