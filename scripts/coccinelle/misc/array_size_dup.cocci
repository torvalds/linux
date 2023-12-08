// SPDX-License-Identifier: GPL-2.0-only
///
/// Check for array_size(), array3_size(), struct_size() duplicates.
/// These patterns are detected:
///  1. An opencoded expression is used before array_size() to compute the same size
///  2. An opencoded expression is used after array_size() to compute the same size
/// From security point of view only first case is relevant. These functions
/// perform arithmetic overflow check. Thus, if we use an opencoded expression
/// before a call to the *_size() function we can miss an overflow.
///
// Confidence: High
// Copyright: (C) 2020 Denis Efremov ISPRAS
// Options: --no-includes --include-headers --no-loops

virtual context
virtual report
virtual org

@as@
expression E1, E2;
@@

array_size(E1, E2)

@as_next@
expression subE1 <= as.E1;
expression subE2 <= as.E2;
expression as.E1, as.E2, E3;
assignment operator aop;
position p1, p2;
@@

* E1 * E2@p1
  ... when != \(subE1\|subE2\) aop E3
      when != &\(subE1\|subE2\)
* array_size(E1, E2)@p2

@script:python depends on report@
p1 << as_next.p1;
p2 << as_next.p2;
@@

msg = "WARNING: array_size is used later (line %s) to compute the same size" % (p2[0].line)
coccilib.report.print_report(p1[0], msg)

@script:python depends on org@
p1 << as_next.p1;
p2 << as_next.p2;
@@

msg = "WARNING: array_size is used later (line %s) to compute the same size" % (p2[0].line)
coccilib.org.print_todo(p1[0], msg)

@as_prev@
expression subE1 <= as.E1;
expression subE2 <= as.E2;
expression as.E1, as.E2, E3;
assignment operator aop;
position p1, p2;
@@

* array_size(E1, E2)@p1
  ... when != \(subE1\|subE2\) aop E3
      when != &\(subE1\|subE2\)
* E1 * E2@p2

@script:python depends on report@
p1 << as_prev.p1;
p2 << as_prev.p2;
@@

msg = "WARNING: array_size is already used (line %s) to compute the same size" % (p1[0].line)
coccilib.report.print_report(p2[0], msg)

@script:python depends on org@
p1 << as_prev.p1;
p2 << as_prev.p2;
@@

msg = "WARNING: array_size is already used (line %s) to compute the same size" % (p1[0].line)
coccilib.org.print_todo(p2[0], msg)

@as3@
expression E1, E2, E3;
@@

array3_size(E1, E2, E3)

@as3_next@
expression subE1 <= as3.E1;
expression subE2 <= as3.E2;
expression subE3 <= as3.E3;
expression as3.E1, as3.E2, as3.E3, E4;
assignment operator aop;
position p1, p2;
@@

* E1 * E2 * E3@p1
  ... when != \(subE1\|subE2\|subE3\) aop E4
      when != &\(subE1\|subE2\|subE3\)
* array3_size(E1, E2, E3)@p2

@script:python depends on report@
p1 << as3_next.p1;
p2 << as3_next.p2;
@@

msg = "WARNING: array3_size is used later (line %s) to compute the same size" % (p2[0].line)
coccilib.report.print_report(p1[0], msg)

@script:python depends on org@
p1 << as3_next.p1;
p2 << as3_next.p2;
@@

msg = "WARNING: array3_size is used later (line %s) to compute the same size" % (p2[0].line)
coccilib.org.print_todo(p1[0], msg)

@as3_prev@
expression subE1 <= as3.E1;
expression subE2 <= as3.E2;
expression subE3 <= as3.E3;
expression as3.E1, as3.E2, as3.E3, E4;
assignment operator aop;
position p1, p2;
@@

* array3_size(E1, E2, E3)@p1
  ... when != \(subE1\|subE2\|subE3\) aop E4
      when != &\(subE1\|subE2\|subE3\)
* E1 * E2 * E3@p2

@script:python depends on report@
p1 << as3_prev.p1;
p2 << as3_prev.p2;
@@

msg = "WARNING: array3_size is already used (line %s) to compute the same size" % (p1[0].line)
coccilib.report.print_report(p2[0], msg)

@script:python depends on org@
p1 << as3_prev.p1;
p2 << as3_prev.p2;
@@

msg = "WARNING: array3_size is already used (line %s) to compute the same size" % (p1[0].line)
coccilib.org.print_todo(p2[0], msg)

@ss@
expression E1, E2, E3;
@@

struct_size(E1, E2, E3)

@ss_next@
expression subE3 <= ss.E3;
expression ss.E1, ss.E2, ss.E3, E4;
assignment operator aop;
position p1, p2;
@@

* E1 * E2 + E3@p1
  ... when != subE3 aop E4
      when != &subE3
* struct_size(E1, E2, E3)@p2

@script:python depends on report@
p1 << ss_next.p1;
p2 << ss_next.p2;
@@

msg = "WARNING: struct_size is used later (line %s) to compute the same size" % (p2[0].line)
coccilib.report.print_report(p1[0], msg)

@script:python depends on org@
p1 << ss_next.p1;
p2 << ss_next.p2;
@@

msg = "WARNING: struct_size is used later (line %s) to compute the same size" % (p2[0].line)
coccilib.org.print_todo(p1[0], msg)

@ss_prev@
expression subE3 <= ss.E3;
expression ss.E1, ss.E2, ss.E3, E4;
assignment operator aop;
position p1, p2;
@@

* struct_size(E1, E2, E3)@p1
  ... when != subE3 aop E4
      when != &subE3
* E1 * E2 + E3@p2

@script:python depends on report@
p1 << ss_prev.p1;
p2 << ss_prev.p2;
@@

msg = "WARNING: struct_size is already used (line %s) to compute the same size" % (p1[0].line)
coccilib.report.print_report(p2[0], msg)

@script:python depends on org@
p1 << ss_prev.p1;
p2 << ss_prev.p2;
@@

msg = "WARNING: struct_size is already used (line %s) to compute the same size" % (p1[0].line)
coccilib.org.print_todo(p2[0], msg)
