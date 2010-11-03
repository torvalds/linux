/// Find functions that refer to GFP_KERNEL but are called with locks held.
/// The proposed change of converting the GFP_KERNEL is not necessarily the
/// correct one.  It may be desired to unlock the lock, or to not call the
/// function under the lock in the first place.
///
// Confidence: Moderate
// Copyright: (C) 2010 Nicolas Palix, DIKU.  GPLv2.
// Copyright: (C) 2010 Julia Lawall, DIKU.  GPLv2.
// Copyright: (C) 2010 Gilles Muller, INRIA/LiP6.  GPLv2.
// URL: http://coccinelle.lip6.fr/
// Comments:
// Options: -no_includes -include_headers

virtual patch

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

@locked@
identifier gfp.fn;
@@

(
read_lock_irq
|
write_lock_irq
|
read_lock_irqsave
|
write_lock_irqsave
|
spin_lock
|
spin_trylock
|
spin_lock_irq
|
spin_lock_irqsave
|
local_irq_disable
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
fn(...)

@depends on locked@
position gfp.p;
@@

- GFP_KERNEL@p
+ GFP_ATOMIC
