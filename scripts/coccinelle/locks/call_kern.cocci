// SPDX-License-Identifier: GPL-2.0-only
/// Find functions that refer to GFP_KERNEL but are called with locks held.
//# The proposed change of converting the GFP_KERNEL is not necessarily the
//# correct one.  It may be desired to unlock the lock, or to not call the
//# function under the lock in the first place.
///
// Confidence: Moderate
// Copyright: (C) 2012 Nicolas Palix.
// Copyright: (C) 2012 Julia Lawall, INRIA/LIP6.
// Copyright: (C) 2012 Gilles Muller, INRIA/LiP6.
// URL: https://coccinelle.gitlabpages.inria.fr/website
// Comments:
// Options: --no-includes --include-headers

virtual patch
virtual context
virtual org
virtual report

@gfp exists@
identifier fn;
position p;
@@

fn(...) {
 ... when != read_unlock_irq(...)
     when != write_unlock_irq(...)
     when != read_unlock_irqrestore(...)
     when != write_unlock_irqrestore(...)
     when != spin_unlock(...)
     when != spin_unlock_irq(...)
     when != spin_unlock_irqrestore(...)
     when != local_irq_enable(...)
     when any
 GFP_KERNEL@p
 ... when any
}

@locked exists@
identifier gfp.fn;
position p1,p2;
@@

(
read_lock_irq@p1
|
write_lock_irq@p1
|
read_lock_irqsave@p1
|
write_lock_irqsave@p1
|
spin_lock@p1
|
spin_trylock@p1
|
spin_lock_irq@p1
|
spin_lock_irqsave@p1
|
local_irq_disable@p1
)
 (...)
...  when != read_unlock_irq(...)
     when != write_unlock_irq(...)
     when != read_unlock_irqrestore(...)
     when != write_unlock_irqrestore(...)
     when != spin_unlock(...)
     when != spin_unlock_irq(...)
     when != spin_unlock_irqrestore(...)
     when != local_irq_enable(...)
fn@p2(...)

@depends on locked && patch@
position gfp.p;
@@

- GFP_KERNEL@p
+ GFP_ATOMIC

@depends on locked && !patch@
position gfp.p;
@@

* GFP_KERNEL@p

@script:python depends on !patch && org@
p << gfp.p;
fn << gfp.fn;
p1 << locked.p1;
p2 << locked.p2;
@@

cocci.print_main("lock",p1)
cocci.print_secs("call",p2)
cocci.print_secs("GFP_KERNEL",p)

@script:python depends on !patch && report@
p << gfp.p;
fn << gfp.fn;
p1 << locked.p1;
p2 << locked.p2;
@@

msg = "ERROR: function %s called on line %s inside lock on line %s but uses GFP_KERNEL" % (fn,p2[0].line,p1[0].line)
coccilib.report.print_report(p[0], msg)
