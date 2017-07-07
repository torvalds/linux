/// Remove unneeded conversion to bool
///
//# Relational and logical operators evaluate to bool,
//# explicit conversion is overly verbose and unneeded.
//
// Copyright: (C) 2016 Andrew F. Davis <afd@ti.com> GPLv2.

virtual patch
virtual context
virtual org
virtual report

//----------------------------------------------------------
//  For patch mode
//----------------------------------------------------------

@depends on patch@
expression A, B;
symbol true, false;
@@

(
  A == B
|
  A != B
|
  A > B
|
  A < B
|
  A >= B
|
  A <= B
|
  A && B
|
  A || B
)
- ? true : false

//----------------------------------------------------------
//  For context mode
//----------------------------------------------------------

@r depends on !patch@
expression A, B;
symbol true, false;
position p;
@@

(
  A == B
|
  A != B
|
  A > B
|
  A < B
|
  A >= B
|
  A <= B
|
  A && B
|
  A || B
)
* ? true : false@p

//----------------------------------------------------------
//  For org mode
//----------------------------------------------------------

@script:python depends on r&&org@
p << r.p;
@@

msg = "WARNING: conversion to bool not needed here"
coccilib.org.print_todo(p[0], msg)

//----------------------------------------------------------
//  For report mode
//----------------------------------------------------------

@script:python depends on r&&report@
p << r.p;
@@

msg = "WARNING: conversion to bool not needed here"
coccilib.report.print_report(p[0], msg)
