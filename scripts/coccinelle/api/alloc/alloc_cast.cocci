/// Remove casting the values returned by memory allocation functions
/// like kmalloc, kzalloc, kmem_cache_alloc, kmem_cache_zalloc etc.
///
//# This makes an effort to find cases of casting of values returned by
//# kmalloc, kzalloc, kcalloc, kmem_cache_alloc, kmem_cache_zalloc,
//# kmem_cache_alloc_node, kmalloc_node and kzalloc_node and removes
//# the casting as it is not required. The result in the patch case may
//# need some reformatting.
//
// Confidence: High
// Copyright: (C) 2014 Himangi Saraogi GPLv2.
// Copyright: (C) 2017 Himanshu Jha GPLv2.
// Comments:
// Options: --no-includes --include-headers
//

virtual context
virtual patch
virtual org
virtual report

@initialize:python@
@@
import re
pattern = '__'
m = re.compile(pattern)

@r1 depends on context || patch@
type T;
@@

  (T *)
  \(kmalloc\|kzalloc\|kcalloc\|kmem_cache_alloc\|kmem_cache_zalloc\|
   kmem_cache_alloc_node\|kmalloc_node\|kzalloc_node\|vmalloc\|vzalloc\|
   dma_alloc_coherent\|devm_kmalloc\|devm_kzalloc\|
   kvmalloc\|kvzalloc\|kvmalloc_node\|kvzalloc_node\|pci_alloc_consistent\|
   pci_zalloc_consistent\|kmem_alloc\|kmem_zalloc\|kmem_zone_alloc\|
   kmem_zone_zalloc\|vmalloc_node\|vzalloc_node\)(...)

//----------------------------------------------------------
//  For context mode
//----------------------------------------------------------

@script:python depends on context@
t << r1.T;
@@

if m.search(t) != None:
        cocci.include_match(False)

@depends on context && r1@
type r1.T;
@@

* (T *)
  \(kmalloc\|kzalloc\|kcalloc\|kmem_cache_alloc\|kmem_cache_zalloc\|
   kmem_cache_alloc_node\|kmalloc_node\|kzalloc_node\|vmalloc\|vzalloc\|
   dma_alloc_coherent\|devm_kmalloc\|devm_kzalloc\|
   kvmalloc\|kvzalloc\|kvmalloc_node\|kvzalloc_node\|pci_alloc_consistent\|
   pci_zalloc_consistent\|kmem_alloc\|kmem_zalloc\|kmem_zone_alloc\|
   kmem_zone_zalloc\|vmalloc_node\|vzalloc_node\)(...)

//----------------------------------------------------------
//  For patch mode
//----------------------------------------------------------

@script:python depends on patch@
t << r1.T;
@@

if m.search(t) != None:
        cocci.include_match(False)

@depends on patch && r1@
type r1.T;
@@

- (T *)
  \(kmalloc\|kzalloc\|kcalloc\|kmem_cache_alloc\|kmem_cache_zalloc\|
   kmem_cache_alloc_node\|kmalloc_node\|kzalloc_node\|vmalloc\|vzalloc\|
   dma_alloc_coherent\|devm_kmalloc\|devm_kzalloc\|
   kvmalloc\|kvzalloc\|kvmalloc_node\|kvzalloc_node\|pci_alloc_consistent\|
   pci_zalloc_consistent\|kmem_alloc\|kmem_zalloc\|kmem_zone_alloc\|
   kmem_zone_zalloc\|vmalloc_node\|vzalloc_node\)(...)

//----------------------------------------------------------
//  For org and report mode
//----------------------------------------------------------

@r2 depends on org || report@
type T;
position p;
@@

 (T@p *)
  \(kmalloc\|kzalloc\|kcalloc\|kmem_cache_alloc\|kmem_cache_zalloc\|
   kmem_cache_alloc_node\|kmalloc_node\|kzalloc_node\|vmalloc\|vzalloc\|
   dma_alloc_coherent\|devm_kmalloc\|devm_kzalloc\|
   kvmalloc\|kvzalloc\|kvmalloc_node\|kvzalloc_node\|pci_alloc_consistent\|
   pci_zalloc_consistent\|kmem_alloc\|kmem_zalloc\|kmem_zone_alloc\|
   kmem_zone_zalloc\|vmalloc_node\|vzalloc_node\)(...)

@script:python depends on org@
p << r2.p;
t << r2.T;
@@

if m.search(t) != None:
	cocci.include_match(False)
else:
	coccilib.org.print_safe_todo(p[0], t)

@script:python depends on report@
p << r2.p;
t << r2.T;
@@

if m.search(t) != None:
	cocci.include_match(False)
else:
	msg="WARNING: casting value returned by memory allocation function to (%s *) is useless." % (t)
	coccilib.report.print_report(p[0], msg)
