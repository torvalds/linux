/// Remove casting the values returned by memory allocation functions
/// like kmalloc, kzalloc, kmem_cache_alloc, kmem_cache_zalloc etc.
///
//# This makes an effort to find cases of casting of values returned by
//# kmalloc, kzalloc, kcalloc, kmem_cache_alloc, kmem_cache_zalloc,
//# kmem_cache_alloc_node, kmalloc_node and kzalloc_node and removes
//# the casting as it is not required. The result in the patch case may
//#need some reformatting.
//
// Confidence: High
// Copyright: 2014, Himangi Saraogi  GPLv2.
// Comments:
// Options: --no-includes --include-headers
//

virtual context
virtual patch
virtual org
virtual report

//----------------------------------------------------------
//  For context mode
//----------------------------------------------------------

@depends on context@
type T;
@@

* (T *)
  \(kmalloc\|kzalloc\|kcalloc\|kmem_cache_alloc\|kmem_cache_zalloc\|
   kmem_cache_alloc_node\|kmalloc_node\|kzalloc_node\)(...)

//----------------------------------------------------------
//  For patch mode
//----------------------------------------------------------

@depends on patch@
type T;
@@

- (T *)
  (\(kmalloc\|kzalloc\|kcalloc\|kmem_cache_alloc\|kmem_cache_zalloc\|
   kmem_cache_alloc_node\|kmalloc_node\|kzalloc_node\)(...))

//----------------------------------------------------------
//  For org and report mode
//----------------------------------------------------------

@r depends on org || report@
type T;
position p;
@@

 (T@p *)\(kmalloc\|kzalloc\|kcalloc\|kmem_cache_alloc\|kmem_cache_zalloc\|
   kmem_cache_alloc_node\|kmalloc_node\|kzalloc_node\)(...)

@script:python depends on org@
p << r.p;
t << r.T;
@@

coccilib.org.print_safe_todo(p[0], t)

@script:python depends on report@
p << r.p;
t << r.T;
@@

msg="WARNING: casting value returned by memory allocation function to (%s *) is useless." % (t)
coccilib.report.print_report(p[0], msg)


