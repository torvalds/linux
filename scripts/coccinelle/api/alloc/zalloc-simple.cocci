///
/// Use zeroing allocator rather than allocator followed by memset with 0
///
/// This considers some simple cases that are common and easy to validate
/// Note in particular that there are no ...s in the rule, so all of the
/// matched code has to be contiguous
///
// Confidence: High
// Copyright: (C) 2009-2010 Julia Lawall, Nicolas Palix, DIKU.  GPLv2.
// Copyright: (C) 2009-2010 Gilles Muller, INRIA/LiP6.  GPLv2.
// Copyright: (C) 2017 Himanshu Jha GPLv2.
// URL: http://coccinelle.lip6.fr/rules/kzalloc.html
// Options: --no-includes --include-headers
//
// Keywords: kmalloc, kzalloc
// Version min: < 2.6.12 kmalloc
// Version min:   2.6.14 kzalloc
//

virtual context
virtual patch
virtual org
virtual report

//----------------------------------------------------------
//  For context mode
//----------------------------------------------------------

@depends on context@
type T, T2;
expression x;
expression E1;
statement S;
@@

* x = (T)\(kmalloc(E1, ...)\|vmalloc(E1)\|dma_alloc_coherent(...,E1,...)\|
  kmalloc_node(E1, ...)\|kmem_cache_alloc(...)\|kmem_alloc(E1, ...)\|
  devm_kmalloc(...,E1,...)\|kvmalloc(E1, ...)\|kvmalloc_node(E1,...)\);
  if ((x==NULL) || ...) S
* memset((T2)x,0,E1);

//----------------------------------------------------------
//  For patch mode
//----------------------------------------------------------

@depends on patch@
type T, T2;
expression x;
expression E1,E2,E3,E4;
statement S;
@@

(
- x = kmalloc(E1,E2);
+ x = kzalloc(E1,E2);
|
- x = (T *)kmalloc(E1,E2);
+ x = kzalloc(E1,E2);
|
- x = (T)kmalloc(E1,E2);
+ x = (T)kzalloc(E1,E2);
|
- x = vmalloc(E1);
+ x = vzalloc(E1);
|
- x = (T *)vmalloc(E1);
+ x = vzalloc(E1);
|
- x = (T)vmalloc(E1);
+ x = (T)vzalloc(E1);
|
- x = dma_alloc_coherent(E2,E1,E3,E4);
+ x = dma_zalloc_coherent(E2,E1,E3,E4);
|
- x = (T *)dma_alloc_coherent(E2,E1,E3,E4);
+ x = dma_zalloc_coherent(E2,E1,E3,E4);
|
- x = (T)dma_alloc_coherent(E2,E1,E3,E4);
+ x = (T)dma_zalloc_coherent(E2,E1,E3,E4);
|
- x = kmalloc_node(E1,E2,E3);
+ x = kzalloc_node(E1,E2,E3);
|
- x = (T *)kmalloc_node(E1,E2,E3);
+ x = kzalloc_node(E1,E2,E3);
|
- x = (T)kmalloc_node(E1,E2,E3);
+ x = (T)kzalloc_node(E1,E2,E3);
|
- x = kmem_cache_alloc(E3,E4);
+ x = kmem_cache_zalloc(E3,E4);
|
- x = (T *)kmem_cache_alloc(E3,E4);
+ x = kmem_cache_zalloc(E3,E4);
|
- x = (T)kmem_cache_alloc(E3,E4);
+ x = (T)kmem_cache_zalloc(E3,E4);
|
- x = kmem_alloc(E1,E2);
+ x = kmem_zalloc(E1,E2);
|
- x = (T *)kmem_alloc(E1,E2);
+ x = kmem_zalloc(E1,E2);
|
- x = (T)kmem_alloc(E1,E2);
+ x = (T)kmem_zalloc(E1,E2);
|
- x = devm_kmalloc(E2,E1,E3);
+ x = devm_kzalloc(E2,E1,E3);
|
- x = (T *)devm_kmalloc(E2,E1,E3);
+ x = devm_kzalloc(E2,E1,E3);
|
- x = (T)devm_kmalloc(E2,E1,E3);
+ x = (T)devm_kzalloc(E2,E1,E3);
|
- x = kvmalloc(E1,E2);
+ x = kvzalloc(E1,E2);
|
- x = (T *)kvmalloc(E1,E2);
+ x = kvzalloc(E1,E2);
|
- x = (T)kvmalloc(E1,E2);
+ x = (T)kvzalloc(E1,E2);
|
- x = kvmalloc_node(E1,E2,E3);
+ x = kvzalloc_node(E1,E2,E3);
|
- x = (T *)kvmalloc_node(E1,E2,E3);
+ x = kvzalloc_node(E1,E2,E3);
|
- x = (T)kvmalloc_node(E1,E2,E3);
+ x = (T)kvzalloc_node(E1,E2,E3);
)
  if ((x==NULL) || ...) S
- memset((T2)x,0,E1);

//----------------------------------------------------------
//  For org mode
//----------------------------------------------------------

@r depends on org || report@
type T, T2;
expression x;
expression E1,E2;
statement S;
position p;
@@

 x = (T)kmalloc@p(E1,E2);
 if ((x==NULL) || ...) S
 memset((T2)x,0,E1);

@script:python depends on org@
p << r.p;
x << r.x;
@@

msg="%s" % (x)
msg_safe=msg.replace("[","@(").replace("]",")")
coccilib.org.print_todo(p[0], msg_safe)

@script:python depends on report@
p << r.p;
x << r.x;
@@

msg="WARNING: kzalloc should be used for %s, instead of kmalloc/memset" % (x)
coccilib.report.print_report(p[0], msg)

//-----------------------------------------------------------------
@r1 depends on org || report@
type T, T2;
expression x;
expression E1;
statement S;
position p;
@@

 x = (T)vmalloc@p(E1);
 if ((x==NULL) || ...) S
 memset((T2)x,0,E1);

@script:python depends on org@
p << r1.p;
x << r1.x;
@@

msg="%s" % (x)
msg_safe=msg.replace("[","@(").replace("]",")")
coccilib.org.print_todo(p[0], msg_safe)

@script:python depends on report@
p << r1.p;
x << r1.x;
@@

msg="WARNING: vzalloc should be used for %s, instead of vmalloc/memset" % (x)
coccilib.report.print_report(p[0], msg)

//-----------------------------------------------------------------
@r2 depends on org || report@
type T, T2;
expression x;
expression E1,E2,E3,E4;
statement S;
position p;
@@

 x = (T)dma_alloc_coherent@p(E2,E1,E3,E4);
 if ((x==NULL) || ...) S
 memset((T2)x,0,E1);

@script:python depends on org@
p << r2.p;
x << r2.x;
@@

msg="%s" % (x)
msg_safe=msg.replace("[","@(").replace("]",")")
coccilib.org.print_todo(p[0], msg_safe)

@script:python depends on report@
p << r2.p;
x << r2.x;
@@

msg="WARNING: dma_zalloc_coherent should be used for %s, instead of dma_alloc_coherent/memset" % (x)
coccilib.report.print_report(p[0], msg)

//-----------------------------------------------------------------
@r3 depends on org || report@
type T, T2;
expression x;
expression E1,E2,E3;
statement S;
position p;
@@

 x = (T)kmalloc_node@p(E1,E2,E3);
 if ((x==NULL) || ...) S
 memset((T2)x,0,E1);

@script:python depends on org@
p << r3.p;
x << r3.x;
@@

msg="%s" % (x)
msg_safe=msg.replace("[","@(").replace("]",")")
coccilib.org.print_todo(p[0], msg_safe)

@script:python depends on report@
p << r3.p;
x << r3.x;
@@

msg="WARNING: kzalloc_node should be used for %s, instead of kmalloc_node/memset" % (x)
coccilib.report.print_report(p[0], msg)

//-----------------------------------------------------------------
@r4 depends on org || report@
type T, T2;
expression x;
expression E1,E2,E3;
statement S;
position p;
@@

 x = (T)kmem_cache_alloc@p(E2,E3);
 if ((x==NULL) || ...) S
 memset((T2)x,0,E1);

@script:python depends on org@
p << r4.p;
x << r4.x;
@@

msg="%s" % (x)
msg_safe=msg.replace("[","@(").replace("]",")")
coccilib.org.print_todo(p[0], msg_safe)

@script:python depends on report@
p << r4.p;
x << r4.x;
@@

msg="WARNING: kmem_cache_zalloc should be used for %s, instead of kmem_cache_alloc/memset" % (x)
coccilib.report.print_report(p[0], msg)

//-----------------------------------------------------------------
@r5 depends on org || report@
type T, T2;
expression x;
expression E1,E2;
statement S;
position p;
@@

 x = (T)kmem_alloc@p(E1,E2);
 if ((x==NULL) || ...) S
 memset((T2)x,0,E1);

@script:python depends on org@
p << r5.p;
x << r5.x;
@@

msg="%s" % (x)
msg_safe=msg.replace("[","@(").replace("]",")")
coccilib.org.print_todo(p[0], msg_safe)

@script:python depends on report@
p << r5.p;
x << r5.x;
@@

msg="WARNING: kmem_zalloc should be used for %s, instead of kmem_alloc/memset" % (x)
coccilib.report.print_report(p[0], msg)

//-----------------------------------------------------------------
@r6 depends on org || report@
type T, T2;
expression x;
expression E1,E2,E3;
statement S;
position p;
@@

 x = (T)devm_kmalloc@p(E2,E1,E3);
 if ((x==NULL) || ...) S
 memset((T2)x,0,E1);

@script:python depends on org@
p << r6.p;
x << r6.x;
@@

msg="%s" % (x)
msg_safe=msg.replace("[","@(").replace("]",")")
coccilib.org.print_todo(p[0], msg_safe)

@script:python depends on report@
p << r6.p;
x << r6.x;
@@

msg="WARNING: devm_kzalloc should be used for %s, instead of devm_kmalloc/memset" % (x)
coccilib.report.print_report(p[0], msg)

//-----------------------------------------------------------------
@r7 depends on org || report@
type T, T2;
expression x;
expression E1,E2;
statement S;
position p;
@@

 x = (T)kvmalloc@p(E1,E2);
 if ((x==NULL) || ...) S
 memset((T2)x,0,E1);

@script:python depends on org@
p << r7.p;
x << r7.x;
@@

msg="%s" % (x)
msg_safe=msg.replace("[","@(").replace("]",")")
coccilib.org.print_todo(p[0], msg_safe)

@script:python depends on report@
p << r7.p;
x << r7.x;
@@

msg="WARNING: kvzalloc should be used for %s, instead of kvmalloc/memset" % (x)
coccilib.report.print_report(p[0], msg)

//-----------------------------------------------------------------
@r9 depends on org || report@
type T, T2;
expression x;
expression E1,E2,E3;
statement S;
position p;
@@

 x = (T)kvmalloc_node@p(E1,E2,E3);
 if ((x==NULL) || ...) S
 memset((T2)x,0,E1);

@script:python depends on org@
p << r9.p;
x << r9.x;
@@

msg="%s" % (x)
msg_safe=msg.replace("[","@(").replace("]",")")
coccilib.org.print_todo(p[0], msg_safe)

@script:python depends on report@
p << r9.p;
x << r9.x;
@@

msg="WARNING: kvzalloc_node should be used for %s, instead of kvmalloc_node/memset" % (x)
coccilib.report.print_report(p[0], msg)
