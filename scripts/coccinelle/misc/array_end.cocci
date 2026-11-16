// SPDX-License-Identifier: GPL-2.0-only
/// Use ARRAY_END instead of an expression derived from ARRAY_SIZE
//
// Confidence: High
// Copyright: 2026, Alejandro Colomar <alx@kernel.org>
// Options: --no-includes --include-headers

virtual patch
virtual context
virtual org
virtual report

@depends on context disable zero_multiple_format@
type T;
T[] a;
expression b;
@@
(
* a + ARRAY_SIZE(a)
|
* &a[0] + ARRAY_SIZE(a)
|
* &a[ARRAY_SIZE(a)]
|
* &a[ARRAY_SIZE(a) - b]
)

@depends on patch disable zero_multiple_format@
type T;
T[] a;
expression b;
@@
(
- (a + ARRAY_SIZE(a))
+ ARRAY_END(a)
|
- (&a[0] + ARRAY_SIZE(a))
+ ARRAY_END(a)
|
- (&a[ARRAY_SIZE(a)])
+ ARRAY_END(a)
|
- (&a[ARRAY_SIZE(a) - b])
+ ARRAY_END(a) - b
)

@r depends on org || report disable zero_multiple_format@
type T;
T[] a;
expression b;
position p;
@@
(
 a@p + ARRAY_SIZE(a)
|
 &a[0]@p + ARRAY_SIZE(a)
|
 &a[ARRAY_SIZE(a)]@p
|
 &a[ARRAY_SIZE(a)@p - b]
)

@script:python depends on org@
p << r.p;
@@

coccilib.org.print_todo(p[0], "WARNING should use ARRAY_END")

@script:python depends on report@
p << r.p;
@@

coccilib.report.print_report(p[0], "WARNING: opportunity for ARRAY_END")
