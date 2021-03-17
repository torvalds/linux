// SPDX-License-Identifier: GPL-2.0-only
/// Find missing unlocks.  This semantic match considers the specific case
/// where the unlock is missing from an if branch, and there is a lock
/// before the if and an unlock after the if.  False positives are due to
/// cases where the if branch represents a case where the function is
/// supposed to exit with the lock held, or where there is some preceding
/// function call that releases the lock.
///
// Confidence: Moderate
// Copyright: (C) 2010-2012 Nicolas Palix.
// Copyright: (C) 2010-2012 Julia Lawall, INRIA/LIP6.
// Copyright: (C) 2010-2012 Gilles Muller, INRIA/LiP6.
// URL: http://coccinelle.lip6.fr/
// Comments:
// Options: --no-includes --include-headers

virtual context
virtual org
virtual report

@prelocked@
position p1,p;
expression E1;
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
|
read_lock_irq@p1
|
write_lock_irq@p1
|
read_lock_irqsave@p1
|
write_lock_irqsave@p1
|
spin_lock_irq@p1
|
spin_lock_irqsave@p1
) (E1@p,...);

@looped@
position r;
@@

for(...;...;...) { <+... return@r ...; ...+> }

@err exists@
expression E1;
position prelocked.p;
position up != prelocked.p1;
position r!=looped.r;
identifier lock,unlock;
@@

*lock(E1@p,...);
... when != E1
    when any
if (...) {
  ... when != E1
*  return@r ...;
}
... when != E1
    when any
*unlock@up(E1,...);

@script:python depends on org@
p << prelocked.p1;
lock << err.lock;
unlock << err.unlock;
p2 << err.r;
@@

cocci.print_main(lock,p)
cocci.print_secs(unlock,p2)

@script:python depends on report@
p << prelocked.p1;
lock << err.lock;
unlock << err.unlock;
p2 << err.r;
@@

msg = "preceding lock on line %s" % (p[0].line)
coccilib.report.print_report(p2[0],msg)
