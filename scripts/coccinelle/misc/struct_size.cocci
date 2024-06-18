// SPDX-License-Identifier: GPL-2.0-only
///
/// Check for code that could use struct_size().
///
// Confidence: Medium
// Author: Jacob Keller <jacob.e.keller@intel.com>
// Copyright: (C) 2023 Intel Corporation
// Options: --no-includes --include-headers

virtual patch
virtual context
virtual org
virtual report

// the overflow Kunit tests have some code which intentionally does not use
// the macros, so we want to ignore this code when reporting potential
// issues.
@overflow_tests@
identifier f = overflow_size_helpers_test;
@@

f

//----------------------------------------------------------
//  For context mode
//----------------------------------------------------------

@depends on !overflow_tests && context@
expression E1, E2;
identifier m;
@@
(
* (sizeof(*E1) + (E2 * sizeof(*E1->m)))
)

//----------------------------------------------------------
//  For patch mode
//----------------------------------------------------------

@depends on !overflow_tests && patch@
expression E1, E2;
identifier m;
@@
(
- (sizeof(*E1) + (E2 * sizeof(*E1->m)))
+ struct_size(E1, m, E2)
)

//----------------------------------------------------------
//  For org and report mode
//----------------------------------------------------------

@r depends on !overflow_tests && (org || report)@
expression E1, E2;
identifier m;
position p;
@@
(
 (sizeof(*E1)@p + (E2 * sizeof(*E1->m)))
)

@script:python depends on org@
p << r.p;
@@

coccilib.org.print_todo(p[0], "WARNING should use struct_size")

@script:python depends on report@
p << r.p;
@@

msg="WARNING: Use struct_size"
coccilib.report.print_report(p[0], msg)

