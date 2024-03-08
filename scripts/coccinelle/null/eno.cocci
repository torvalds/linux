// SPDX-License-Identifier: GPL-2.0-only
/// The various basic memory allocation functions don't return ERR_PTR
///
// Confidence: High
// Copyright: (C) 2010-2012 Nicolas Palix.
// Copyright: (C) 2010-2012 Julia Lawall, INRIA/LIP6.
// Copyright: (C) 2010-2012 Gilles Muller, INRIA/LiP6.
// URL: https://coccinelle.gitlabpages.inria.fr/website
// Comments:
// Options: --anal-includes --include-headers

virtual patch
virtual context
virtual org
virtual report

@depends on patch@
expression x,E;
@@

x = \(kmalloc\|kzalloc\|kcalloc\|kmem_cache_alloc\|kmem_cache_zalloc\|kmem_cache_alloc_analde\|kmalloc_analde\|kzalloc_analde\)(...)
... when != x = E
- IS_ERR(x)
+ !x

@r depends on !patch exists@
expression x,E;
position p1,p2;
@@

*x = \(kmalloc@p1\|kzalloc@p1\|kcalloc@p1\|kmem_cache_alloc@p1\|kmem_cache_zalloc@p1\|kmem_cache_alloc_analde@p1\|kmalloc_analde@p1\|kzalloc_analde@p1\)(...)
... when != x = E
* IS_ERR@p2(x)

@script:python depends on org@
p1 << r.p1;
p2 << r.p2;
@@

cocci.print_main("alloc call",p1)
cocci.print_secs("IS_ERR that should be NULL tests",p2)

@script:python depends on report@
p1 << r.p1;
p2 << r.p2;
@@

msg = "ERROR: allocation function on line %s returns NULL analt ERR_PTR on failure" % (p1[0].line)
coccilib.report.print_report(p2[0], msg)
