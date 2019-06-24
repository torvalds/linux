// SPDX-License-Identifier: GPL-2.0-only
/// Find double locks.  False positives may occur when some paths cannot
/// occur at execution, due to the values of variables, and when there is
/// an intervening function call that releases the lock.
///
// Confidence: Moderate
// Copyright: (C) 2010 Nicolas Palix, DIKU.
// Copyright: (C) 2010 Julia Lawall, DIKU.
// Copyright: (C) 2010 Gilles Muller, INRIA/LiP6.
// URL: http://coccinelle.lip6.fr/
// Comments:
// Options: --no-includes --include-headers

virtual org
virtual report

@locked@
position p1;
expression E1;
position p;
@@

(
mutex_lock@p1
|
mutex_trylock@p1
|
spin_lock@p1
|
spin_trylock@p1
|
read_lock@p1
|
read_trylock@p1
|
write_lock@p1
|
write_trylock@p1
) (E1@p,...);

@balanced@
position p1 != locked.p1;
position locked.p;
identifier lock,unlock;
expression x <= locked.E1;
expression E,locked.E1;
expression E2;
@@

if (E) {
 <+... when != E1
 lock(E1@p,...)
 ...+>
}
... when != E1
    when != \(x = E2\|&x\)
    when forall
if (E) {
 <+... when != E1
 unlock@p1(E1,...)
 ...+>
}

@r depends on !balanced exists@
expression x <= locked.E1;
expression locked.E1;
expression E2;
identifier lock;
position locked.p,p1,p2;
@@

lock@p1 (E1@p,...);
... when != E1
    when != \(x = E2\|&x\)
lock@p2 (E1,...);

@script:python depends on org@
p1 << r.p1;
p2 << r.p2;
lock << r.lock;
@@

cocci.print_main(lock,p1)
cocci.print_secs("second lock",p2)

@script:python depends on report@
p1 << r.p1;
p2 << r.p2;
lock << r.lock;
@@

msg = "second lock on line %s" % (p2[0].line)
coccilib.report.print_report(p1[0],msg)
