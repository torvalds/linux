// SPDX-License-Identifier: GPL-2.0-only
/// Find uses of standard freeing functons on values allocated using devm_
/// functions.  Values allocated using the devm_functions are freed when
/// the device is detached, and thus the use of the standard freeing
/// function would cause a double free.
/// See Documentation/driver-api/driver-model/devres.rst for more information.
///
/// A difficulty of detecting this problem is that the standard freeing
/// function might be called from a different function than the one
/// containing the allocation function.  It is thus necessary to make the
/// connection between the allocation function and the freeing function.
/// Here this is done using the specific argument text, which is prone to
/// false positives.  There is no rule for the request_region and
/// request_mem_region variants because this heuristic seems to be a bit
/// less reliable in these cases.
///
// Confidence: Moderate
// Copyright: (C) 2011 Julia Lawall, INRIA/LIP6.
// Copyright: (C) 2011 Gilles Muller, INRIA/LiP6.
// URL: http://coccinelle.lip6.fr/
// Comments:
// Options: --no-includes --include-headers

virtual org
virtual report
virtual context

@r depends on context || org || report@
expression x;
@@

(
 x = devm_kmalloc(...)
|
 x = devm_kvasprintf(...)
|
 x = devm_kasprintf(...)
|
 x = devm_kzalloc(...)
|
 x = devm_kmalloc_array(...)
|
 x = devm_kcalloc(...)
|
 x = devm_kstrdup(...)
|
 x = devm_kmemdup(...)
|
 x = devm_get_free_pages(...)
|
 x = devm_request_irq(...)
|
 x = devm_ioremap(...)
|
 x = devm_ioremap_nocache(...)
|
 x = devm_ioport_map(...)
)

@safe depends on context || org || report exists@
expression x;
position p;
@@

(
 x = kmalloc(...)
|
 x = kvasprintf(...)
|
 x = kasprintf(...)
|
 x = kzalloc(...)
|
 x = kmalloc_array(...)
|
 x = kcalloc(...)
|
 x = kstrdup(...)
|
 x = kmemdup(...)
|
 x = get_free_pages(...)
|
 x = request_irq(...)
|
 x = ioremap(...)
|
 x = ioremap_nocache(...)
|
 x = ioport_map(...)
)
...
(
 kfree@p(x)
|
 kzfree@p(x)
|
 __krealloc@p(x, ...)
|
 krealloc@p(x, ...)
|
 free_pages@p(x, ...)
|
 free_page@p(x)
|
 free_irq@p(x)
|
 iounmap@p(x)
|
 ioport_unmap@p(x)
)

@pb@
expression r.x;
position p != safe.p;
@@

(
* kfree@p(x)
|
* kzfree@p(x)
|
* __krealloc@p(x, ...)
|
* krealloc@p(x, ...)
|
* free_pages@p(x, ...)
|
* free_page@p(x)
|
* free_irq@p(x)
|
* iounmap@p(x)
|
* ioport_unmap@p(x)
)

@script:python depends on org@
p << pb.p;
@@

msg="WARNING: invalid free of devm_ allocated data"
coccilib.org.print_todo(p[0], msg)

@script:python depends on report@
p << pb.p;
@@

msg="WARNING: invalid free of devm_ allocated data"
coccilib.report.print_report(p[0], msg)

