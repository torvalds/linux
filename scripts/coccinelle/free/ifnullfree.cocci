// SPDX-License-Identifier: GPL-2.0-only
/// NULL check before some freeing functions is not needed.
///
/// Based on checkpatch warning
/// "kfree(NULL) is safe this check is probably not required"
/// and kfreeaddr.cocci by Julia Lawall.
///
// Copyright: (C) 2014 Fabian Frederick.
// Comments: -
// Options: --no-includes --include-headers

virtual patch
virtual org
virtual report
virtual context

@r2 depends on patch@
expression E;
@@
- if (E != NULL)
(
  kfree(E);
|
  kvfree(E);
|
  kfree_sensitive(E);
|
  kvfree_sensitive(E, ...);
|
  vfree(E);
|
  debugfs_remove(E);
|
  debugfs_remove_recursive(E);
|
  usb_free_urb(E);
|
  kmem_cache_destroy(E);
|
  mempool_destroy(E);
|
  dma_pool_destroy(E);
)

@r depends on context || report || org @
expression E;
position p;
@@

* if (E != NULL)
*	\(kfree@p\|kvfree@p\|kfree_sensitive@p\|kvfree_sensitive@p\|vfree@p\|
*         debugfs_remove@p\|debugfs_remove_recursive@p\|
*         usb_free_urb@p\|kmem_cache_destroy@p\|mempool_destroy@p\|
*         dma_pool_destroy@p\)(E, ...);

@script:python depends on org@
p << r.p;
@@

cocci.print_main("NULL check before that freeing function is not needed", p)

@script:python depends on report@
p << r.p;
@@

msg = "WARNING: NULL check before some freeing functions is not needed."
coccilib.report.print_report(p[0], msg)
