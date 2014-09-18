/// Use ARRAY_SIZE instead of dividing sizeof array with sizeof an element
///
//# This makes an effort to find cases where ARRAY_SIZE can be used such as
//# where there is a division of sizeof the array by the sizeof its first
//# element or by any indexed element or the element type. It replaces the
//# division of the two sizeofs by ARRAY_SIZE.
//
// Confidence: High
// Copyright: (C) 2014 Himangi Saraogi.  GPLv2.
// Comments:
// Options: --no-includes --include-headers

virtual patch
virtual context
virtual org
virtual report

@i@
@@

#include <linux/kernel.h>

//----------------------------------------------------------
//  For context mode
//----------------------------------------------------------

@depends on i&&context@
type T;
T[] E;
@@
(
* (sizeof(E)/sizeof(*E))
|
* (sizeof(E)/sizeof(E[...]))
|
* (sizeof(E)/sizeof(T))
)

//----------------------------------------------------------
//  For patch mode
//----------------------------------------------------------

@depends on i&&patch@
type T;
T[] E;
@@
(
- (sizeof(E)/sizeof(*E))
+ ARRAY_SIZE(E)
|
- (sizeof(E)/sizeof(E[...]))
+ ARRAY_SIZE(E)
|
- (sizeof(E)/sizeof(T))
+ ARRAY_SIZE(E)
)

//----------------------------------------------------------
//  For org and report mode
//----------------------------------------------------------

@r@
type T;
T[] E;
position p;
@@
(
 (sizeof(E)@p /sizeof(*E))
|
 (sizeof(E)@p /sizeof(E[...]))
|
 (sizeof(E)@p /sizeof(T))
)

@script:python depends on i&&org@
p << r.p;
@@

coccilib.org.print_todo(p[0], "WARNING should use ARRAY_SIZE")

@script:python depends on i&&report@
p << r.p;
@@

msg="WARNING: Use ARRAY_SIZE"
coccilib.report.print_report(p[0], msg)

