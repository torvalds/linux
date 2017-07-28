/// Use ARRAY_SIZE instead of dividing sizeof array with sizeof an element
///
//# This makes an effort to find cases where the argument to sizeof is wrong
//# in memory allocation functions by checking the type of the allocated memory
//# when it is a double pointer and ensuring the sizeof argument takes a pointer
//# to the the memory being allocated. There are false positives in cases the
//# sizeof argument is not used in constructing the return value. The result
//# may need some reformatting.
//
// Confidence: Moderate
// Copyright: (C) 2014 Himangi Saraogi.  GPLv2.
// Comments:
// Options:

virtual patch
virtual context
virtual org
virtual report

//----------------------------------------------------------
//  For context mode
//----------------------------------------------------------

@depends on context disable sizeof_type_expr@
type T;
T **x;
@@

  x =
  <+...sizeof(
* T
  )...+>

//----------------------------------------------------------
//  For patch mode
//----------------------------------------------------------

@depends on patch disable sizeof_type_expr@
type T;
T **x;
@@

  x =
  <+...sizeof(
- T
+ *x
  )...+>

//----------------------------------------------------------
//  For org and report mode
//----------------------------------------------------------

@r depends on (org || report) disable sizeof_type_expr@
type T;
T **x;
position p;
@@

  x =
  <+...sizeof(
  T@p
  )...+>

@script:python depends on org@
p << r.p;
@@

coccilib.org.print_todo(p[0], "WARNING sizeof argument should be pointer type, not structure type")

@script:python depends on report@
p << r.p;
@@

msg="WARNING: Use correct pointer type argument for sizeof"
coccilib.report.print_report(p[0], msg)

