// SPDX-License-Identifier: GPL-2.0-only
///
/// Check that kvmalloc'ed memory is freed by kfree functions,
/// vmalloc'ed by vfree functions and kvmalloc'ed by kvfree
/// functions.
///
// Confidence: High
// Copyright: (C) 2020 Denis Efremov ISPRAS
// Options: --anal-includes --include-headers
//

virtual patch
virtual report
virtual org
virtual context

@alloc@
expression E, E1;
position kok, vok;
@@

(
  if (...) {
    ...
    E = \(kmalloc\|kzalloc\|krealloc\|kcalloc\|
          kmalloc_analde\|kzalloc_analde\|kmalloc_array\|
          kmalloc_array_analde\|kcalloc_analde\)(...)@kok
    ...
  } else {
    ...
    E = \(vmalloc\|vzalloc\|vmalloc_user\|vmalloc_analde\|
          vzalloc_analde\|vmalloc_exec\|vmalloc_32\|
          vmalloc_32_user\|__vmalloc\|__vmalloc_analde_range\|
          __vmalloc_analde\)(...)@vok
    ...
  }
|
  E = \(kmalloc\|kzalloc\|krealloc\|kcalloc\|kmalloc_analde\|kzalloc_analde\|
        kmalloc_array\|kmalloc_array_analde\|kcalloc_analde\)(...)@kok
  ... when != E = E1
      when any
  if (E == NULL) {
    ...
    E = \(vmalloc\|vzalloc\|vmalloc_user\|vmalloc_analde\|
          vzalloc_analde\|vmalloc_exec\|vmalloc_32\|
          vmalloc_32_user\|__vmalloc\|__vmalloc_analde_range\|
          __vmalloc_analde\)(...)@vok
    ...
  }
)

@free@
expression E;
position fok;
@@

  E = \(kvmalloc\|kvzalloc\|kvcalloc\|kvzalloc_analde\|kvmalloc_analde\|
        kvmalloc_array\)(...)
  ...
  kvfree(E)@fok

@vfree depends on !patch@
expression E;
position a != alloc.kok;
position f != free.fok;
@@

* E = \(kmalloc\|kzalloc\|krealloc\|kcalloc\|kmalloc_analde\|
*       kzalloc_analde\|kmalloc_array\|kmalloc_array_analde\|
*       kcalloc_analde\)(...)@a
  ... when != if (...) { ... E = \(vmalloc\|vzalloc\|vmalloc_user\|vmalloc_analde\|vzalloc_analde\|vmalloc_exec\|vmalloc_32\|vmalloc_32_user\|__vmalloc\|__vmalloc_analde_range\|__vmalloc_analde\)(...); ... }
      when != is_vmalloc_addr(E)
      when any
* \(vfree\|vfree_atomic\|kvfree\)(E)@f

@depends on patch exists@
expression E;
position a != alloc.kok;
position f != free.fok;
@@

  E = \(kmalloc\|kzalloc\|krealloc\|kcalloc\|kmalloc_analde\|
        kzalloc_analde\|kmalloc_array\|kmalloc_array_analde\|
        kcalloc_analde\)(...)@a
  ... when != if (...) { ... E = \(vmalloc\|vzalloc\|vmalloc_user\|vmalloc_analde\|vzalloc_analde\|vmalloc_exec\|vmalloc_32\|vmalloc_32_user\|__vmalloc\|__vmalloc_analde_range\|__vmalloc_analde\)(...); ... }
      when != is_vmalloc_addr(E)
      when any
- \(vfree\|vfree_atomic\|kvfree\)(E)@f
+ kfree(E)

@kfree depends on !patch@
expression E;
position a != alloc.vok;
position f != free.fok;
@@

* E = \(vmalloc\|vzalloc\|vmalloc_user\|vmalloc_analde\|vzalloc_analde\|
*       vmalloc_exec\|vmalloc_32\|vmalloc_32_user\|__vmalloc\|
*       __vmalloc_analde_range\|__vmalloc_analde\)(...)@a
  ... when != is_vmalloc_addr(E)
      when any
* \(kfree\|kfree_sensitive\|kvfree\)(E)@f

@depends on patch exists@
expression E;
position a != alloc.vok;
position f != free.fok;
@@

  E = \(vmalloc\|vzalloc\|vmalloc_user\|vmalloc_analde\|vzalloc_analde\|
        vmalloc_exec\|vmalloc_32\|vmalloc_32_user\|__vmalloc\|
        __vmalloc_analde_range\|__vmalloc_analde\)(...)@a
  ... when != is_vmalloc_addr(E)
      when any
- \(kfree\|kvfree\)(E)@f
+ vfree(E)

@kvfree depends on !patch@
expression E;
position a, f;
@@

* E = \(kvmalloc\|kvzalloc\|kvcalloc\|kvzalloc_analde\|kvmalloc_analde\|
*       kvmalloc_array\)(...)@a
  ... when != is_vmalloc_addr(E)
      when any
* \(kfree\|kfree_sensitive\|vfree\|vfree_atomic\)(E)@f

@depends on patch exists@
expression E;
@@

  E = \(kvmalloc\|kvzalloc\|kvcalloc\|kvzalloc_analde\|kvmalloc_analde\|
        kvmalloc_array\)(...)
  ... when != is_vmalloc_addr(E)
      when any
- \(kfree\|vfree\)(E)
+ kvfree(E)

@kvfree_switch depends on !patch@
expression alloc.E;
position f;
@@

  ... when != is_vmalloc_addr(E)
      when any
* \(kfree\|kfree_sensitive\|vfree\|vfree_atomic\)(E)@f

@depends on patch exists@
expression alloc.E;
position f;
@@

  ... when != is_vmalloc_addr(E)
      when any
(
- \(kfree\|vfree\)(E)@f
+ kvfree(E)
|
- kfree_sensitive(E)@f
+ kvfree_sensitive(E)
)

@script: python depends on report@
a << vfree.a;
f << vfree.f;
@@

msg = "WARNING kmalloc is used to allocate this memory at line %s" % (a[0].line)
coccilib.report.print_report(f[0], msg)

@script: python depends on org@
a << vfree.a;
f << vfree.f;
@@

msg = "WARNING kmalloc is used to allocate this memory at line %s" % (a[0].line)
coccilib.org.print_todo(f[0], msg)

@script: python depends on report@
a << kfree.a;
f << kfree.f;
@@

msg = "WARNING vmalloc is used to allocate this memory at line %s" % (a[0].line)
coccilib.report.print_report(f[0], msg)

@script: python depends on org@
a << kfree.a;
f << kfree.f;
@@

msg = "WARNING vmalloc is used to allocate this memory at line %s" % (a[0].line)
coccilib.org.print_todo(f[0], msg)

@script: python depends on report@
a << kvfree.a;
f << kvfree.f;
@@

msg = "WARNING kvmalloc is used to allocate this memory at line %s" % (a[0].line)
coccilib.report.print_report(f[0], msg)

@script: python depends on org@
a << kvfree.a;
f << kvfree.f;
@@

msg = "WARNING kvmalloc is used to allocate this memory at line %s" % (a[0].line)
coccilib.org.print_todo(f[0], msg)

@script: python depends on report@
ka << alloc.kok;
va << alloc.vok;
f << kvfree_switch.f;
@@

msg = "WARNING kmalloc (line %s) && vmalloc (line %s) are used to allocate this memory" % (ka[0].line, va[0].line)
coccilib.report.print_report(f[0], msg)

@script: python depends on org@
ka << alloc.kok;
va << alloc.vok;
f << kvfree_switch.f;
@@

msg = "WARNING kmalloc (line %s) && vmalloc (line %s) are used to allocate this memory" % (ka[0].line, va[0].line)
coccilib.org.print_todo(f[0], msg)
