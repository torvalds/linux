// SPDX-License-Identifier: GPL-2.0-only
// Check if refcount_t type and API should be used
// instead of atomic_t type when dealing with refcounters
//
// Copyright (c) 2016-2017, Elena Reshetova, Intel Corporation
//
// Confidence: Moderate
// URL: https://coccinelle.gitlabpages.inria.fr/website
// Options: --include-headers --very-quiet

virtual report

@r1 exists@
identifier a, x;
position p1, p2;
identifier fname =~ ".*free.*";
identifier fname2 =~ ".*destroy.*";
identifier fname3 =~ ".*del.*";
identifier fname4 =~ ".*queue_work.*";
identifier fname5 =~ ".*schedule_work.*";
identifier fname6 =~ ".*call_rcu.*";

@@

(
 atomic_dec_and_test@p1(&(a)->x)
|
 atomic_dec_and_lock@p1(&(a)->x, ...)
|
 atomic_long_dec_and_lock@p1(&(a)->x, ...)
|
 atomic_long_dec_and_test@p1(&(a)->x)
|
 atomic64_dec_and_test@p1(&(a)->x)
|
 local_dec_and_test@p1(&(a)->x)
)
...
(
 fname@p2(a, ...);
|
 fname2@p2(...);
|
 fname3@p2(...);
|
 fname4@p2(...);
|
 fname5@p2(...);
|
 fname6@p2(...);
)


@script:python depends on report@
p1 << r1.p1;
p2 << r1.p2;
@@
msg = "atomic_dec_and_test variation before object free at line %s."
coccilib.report.print_report(p1[0], msg % (p2[0].line))

@r4 exists@
identifier a, x, y;
position p1, p2;
identifier fname =~ ".*free.*";

@@

(
 atomic_dec_and_test@p1(&(a)->x)
|
 atomic_dec_and_lock@p1(&(a)->x, ...)
|
 atomic_long_dec_and_lock@p1(&(a)->x, ...)
|
 atomic_long_dec_and_test@p1(&(a)->x)
|
 atomic64_dec_and_test@p1(&(a)->x)
|
 local_dec_and_test@p1(&(a)->x)
)
...
y=a
...
fname@p2(y, ...);


@script:python depends on report@
p1 << r4.p1;
p2 << r4.p2;
@@
msg = "atomic_dec_and_test variation before object free at line %s."
coccilib.report.print_report(p1[0], msg % (p2[0].line))

@r2 exists@
identifier a, x;
position p1;
@@

(
atomic_add_unless(&(a)->x,-1,1)@p1
|
atomic_long_add_unless(&(a)->x,-1,1)@p1
|
atomic64_add_unless(&(a)->x,-1,1)@p1
)

@script:python depends on report@
p1 << r2.p1;
@@
msg = "atomic_add_unless"
coccilib.report.print_report(p1[0], msg)

@r3 exists@
identifier x;
position p1;
@@

(
x = atomic_add_return@p1(-1, ...);
|
x = atomic_long_add_return@p1(-1, ...);
|
x = atomic64_add_return@p1(-1, ...);
)

@script:python depends on report@
p1 << r3.p1;
@@
msg = "x = atomic_add_return(-1, ...)"
coccilib.report.print_report(p1[0], msg)
