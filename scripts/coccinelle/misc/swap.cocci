// SPDX-License-Identifier: GPL-2.0-only
///
/// Check for opencoded swap() implementation.
///
// Confidence: High
// Copyright: (C) 2021 Denis Efremov ISPRAS
// Options: --no-includes --include-headers
//
// Keywords: swap
//

virtual patch
virtual org
virtual report
virtual context

@rvar depends on !patch@
identifier tmp;
expression a, b;
type T;
position p;
@@

(
* T tmp;
|
* T tmp = 0;
|
* T *tmp = NULL;
)
... when != tmp
* tmp = a;
* a = b;@p
* b = tmp;
... when != tmp

@r depends on !patch@
identifier tmp;
expression a, b;
position p != rvar.p;
@@

* tmp = a;
* a = b;@p
* b = tmp;

@rpvar depends on patch@
identifier tmp;
expression a, b;
type T;
@@

(
- T tmp;
|
- T tmp = 0;
|
- T *tmp = NULL;
)
... when != tmp
- tmp = a;
- a = b;
- b = tmp
+ swap(a, b)
  ;
... when != tmp

@rp depends on patch@
identifier tmp;
expression a, b;
@@

- tmp = a;
- a = b;
- b = tmp
+ swap(a, b)
  ;

@depends on patch && (rpvar || rp)@
@@

(
  for (...;...;...)
- {
	swap(...);
- }
|
  while (...)
- {
	swap(...);
- }
|
  if (...)
- {
	swap(...);
- }
)


@script:python depends on report@
p << r.p;
@@

coccilib.report.print_report(p[0], "WARNING opportunity for swap()")

@script:python depends on org@
p << r.p;
@@

coccilib.org.print_todo(p[0], "WARNING opportunity for swap()")

@script:python depends on report@
p << rvar.p;
@@

coccilib.report.print_report(p[0], "WARNING opportunity for swap()")

@script:python depends on org@
p << rvar.p;
@@

coccilib.org.print_todo(p[0], "WARNING opportunity for swap()")
