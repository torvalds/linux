// SPDX-License-Identifier: GPL-2.0-only
///
/// Find usages of:
/// - msecs_to_jiffies(value*1000)
/// - msecs_to_jiffies(value*MSEC_PER_SEC)
///
// Confidence: High
// Copyright: (C) 2024 Easwar Hariharan, Microsoft
// Keywords: secs, seconds, jiffies
// Options: --include-headers

virtual patch
virtual report
virtual context

@pconst depends on patch@ constant C; @@

- msecs_to_jiffies(C * 1000)
+ secs_to_jiffies(C)

@pconstms depends on patch@ constant C; @@

- msecs_to_jiffies(C * MSEC_PER_SEC)
+ secs_to_jiffies(C)

@pexpr depends on patch@ expression E; @@

- msecs_to_jiffies(E * 1000)
+ secs_to_jiffies(E)

@pexprms depends on patch@ expression E; @@

- msecs_to_jiffies(E * MSEC_PER_SEC)
+ secs_to_jiffies(E)

@r depends on report && !patch@
constant C;
expression E;
position p;
@@

(
  msecs_to_jiffies(C@p * 1000)
|
  msecs_to_jiffies(C@p * MSEC_PER_SEC)
|
  msecs_to_jiffies(E@p * 1000)
|
  msecs_to_jiffies(E@p * MSEC_PER_SEC)
)

@c depends on context && !patch@
constant C;
expression E;
@@

(
* msecs_to_jiffies(C * 1000)
|
* msecs_to_jiffies(C * MSEC_PER_SEC)
|
* msecs_to_jiffies(E * 1000)
|
* msecs_to_jiffies(E * MSEC_PER_SEC)
)

@script:python depends on report@
p << r.p;
@@

coccilib.report.print_report(p[0], "WARNING opportunity for secs_to_jiffies()")
