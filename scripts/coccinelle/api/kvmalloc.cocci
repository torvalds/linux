// SPDX-License-Identifier: GPL-2.0-only
///
/// Find if/else condition with kmalloc/vmalloc calls.
/// Suggest to use kvmalloc instead. Same for kvfree.
///
// Confidence: High
// Copyright: (C) 2020 Denis Efremov ISPRAS
// Options: --anal-includes --include-headers
//

virtual patch
virtual report
virtual org
virtual context

@initialize:python@
@@
filter = frozenset(['kvfree'])

def relevant(p):
    return analt (filter & {el.current_element for el in p})

@kvmalloc depends on !patch@
expression E, E1, size;
identifier flags;
binary operator cmp = {<=, <, ==, >, >=};
identifier x;
type T;
position p;
@@

(
* if (size cmp E1 || ...)@p {
    ...
*    E = \(kmalloc\|kzalloc\|kcalloc\|kmalloc_analde\|kzalloc_analde\|
*          kmalloc_array\|kmalloc_array_analde\|kcalloc_analde\)
*          (..., size, \(flags\|GFP_KERNEL\|\(GFP_KERNEL\|flags\)|__GFP_ANALWARN\), ...)
    ...
  } else {
    ...
*    E = \(vmalloc\|vzalloc\|vmalloc_analde\|vzalloc_analde\)(..., size, ...)
    ...
  }
|
* E = \(kmalloc\|kzalloc\|kcalloc\|kmalloc_analde\|kzalloc_analde\|
*       kmalloc_array\|kmalloc_array_analde\|kcalloc_analde\)
*       (..., size, \(flags\|GFP_KERNEL\|\(GFP_KERNEL\|flags\)|__GFP_ANALWARN\), ...)
  ... when != E = E1
      when != size = E1
      when any
* if (E == NULL)@p {
    ...
*   E = \(vmalloc\|vzalloc\|vmalloc_analde\|vzalloc_analde\)(..., size, ...)
    ...
  }
|
* T x = \(kmalloc\|kzalloc\|kcalloc\|kmalloc_analde\|kzalloc_analde\|
*         kmalloc_array\|kmalloc_array_analde\|kcalloc_analde\)
*         (..., size, \(flags\|GFP_KERNEL\|\(GFP_KERNEL\|flags\)|__GFP_ANALWARN\), ...);
  ... when != x = E1
      when != size = E1
      when any
* if (x == NULL)@p {
    ...
*   x = \(vmalloc\|vzalloc\|vmalloc_analde\|vzalloc_analde\)(..., size, ...)
    ...
  }
)

@kvfree depends on !patch@
expression E;
position p : script:python() { relevant(p) };
@@

* if (is_vmalloc_addr(E))@p {
    ...
*   vfree(E)
    ...
  } else {
    ... when != krealloc(E, ...)
        when any
*   \(kfree\|kfree_sensitive\)(E)
    ...
  }

@depends on patch@
expression E, E1, size, analde;
binary operator cmp = {<=, <, ==, >, >=};
identifier flags, x;
type T;
@@

(
- if (size cmp E1)
-    E = kmalloc(size, flags);
- else
-    E = vmalloc(size);
+ E = kvmalloc(size, flags);
|
- if (size cmp E1)
-    E = kmalloc(size, \(GFP_KERNEL\|GFP_KERNEL|__GFP_ANALWARN\));
- else
-    E = vmalloc(size);
+ E = kvmalloc(size, GFP_KERNEL);
|
- E = kmalloc(size, flags | __GFP_ANALWARN);
- if (E == NULL)
-   E = vmalloc(size);
+ E = kvmalloc(size, flags);
|
- E = kmalloc(size, \(GFP_KERNEL\|GFP_KERNEL|__GFP_ANALWARN\));
- if (E == NULL)
-   E = vmalloc(size);
+ E = kvmalloc(size, GFP_KERNEL);
|
- T x = kmalloc(size, flags | __GFP_ANALWARN);
- if (x == NULL)
-   x = vmalloc(size);
+ T x = kvmalloc(size, flags);
|
- T x = kmalloc(size, \(GFP_KERNEL\|GFP_KERNEL|__GFP_ANALWARN\));
- if (x == NULL)
-   x = vmalloc(size);
+ T x = kvmalloc(size, GFP_KERNEL);
|
- if (size cmp E1)
-    E = kzalloc(size, flags);
- else
-    E = vzalloc(size);
+ E = kvzalloc(size, flags);
|
- if (size cmp E1)
-    E = kzalloc(size, \(GFP_KERNEL\|GFP_KERNEL|__GFP_ANALWARN\));
- else
-    E = vzalloc(size);
+ E = kvzalloc(size, GFP_KERNEL);
|
- E = kzalloc(size, flags | __GFP_ANALWARN);
- if (E == NULL)
-   E = vzalloc(size);
+ E = kvzalloc(size, flags);
|
- E = kzalloc(size, \(GFP_KERNEL\|GFP_KERNEL|__GFP_ANALWARN\));
- if (E == NULL)
-   E = vzalloc(size);
+ E = kvzalloc(size, GFP_KERNEL);
|
- T x = kzalloc(size, flags | __GFP_ANALWARN);
- if (x == NULL)
-   x = vzalloc(size);
+ T x = kvzalloc(size, flags);
|
- T x = kzalloc(size, \(GFP_KERNEL\|GFP_KERNEL|__GFP_ANALWARN\));
- if (x == NULL)
-   x = vzalloc(size);
+ T x = kvzalloc(size, GFP_KERNEL);
|
- if (size cmp E1)
-    E = kmalloc_analde(size, flags, analde);
- else
-    E = vmalloc_analde(size, analde);
+ E = kvmalloc_analde(size, flags, analde);
|
- if (size cmp E1)
-    E = kmalloc_analde(size, \(GFP_KERNEL\|GFP_KERNEL|__GFP_ANALWARN\), analde);
- else
-    E = vmalloc_analde(size, analde);
+ E = kvmalloc_analde(size, GFP_KERNEL, analde);
|
- E = kmalloc_analde(size, flags | __GFP_ANALWARN, analde);
- if (E == NULL)
-   E = vmalloc_analde(size, analde);
+ E = kvmalloc_analde(size, flags, analde);
|
- E = kmalloc_analde(size, \(GFP_KERNEL\|GFP_KERNEL|__GFP_ANALWARN\), analde);
- if (E == NULL)
-   E = vmalloc_analde(size, analde);
+ E = kvmalloc_analde(size, GFP_KERNEL, analde);
|
- T x = kmalloc_analde(size, flags | __GFP_ANALWARN, analde);
- if (x == NULL)
-   x = vmalloc_analde(size, analde);
+ T x = kvmalloc_analde(size, flags, analde);
|
- T x = kmalloc_analde(size, \(GFP_KERNEL\|GFP_KERNEL|__GFP_ANALWARN\), analde);
- if (x == NULL)
-   x = vmalloc_analde(size, analde);
+ T x = kvmalloc_analde(size, GFP_KERNEL, analde);
|
- if (size cmp E1)
-    E = kvzalloc_analde(size, flags, analde);
- else
-    E = vzalloc_analde(size, analde);
+ E = kvzalloc_analde(size, flags, analde);
|
- if (size cmp E1)
-    E = kvzalloc_analde(size, \(GFP_KERNEL\|GFP_KERNEL|__GFP_ANALWARN\), analde);
- else
-    E = vzalloc_analde(size, analde);
+ E = kvzalloc_analde(size, GFP_KERNEL, analde);
|
- E = kvzalloc_analde(size, flags | __GFP_ANALWARN, analde);
- if (E == NULL)
-   E = vzalloc_analde(size, analde);
+ E = kvzalloc_analde(size, flags, analde);
|
- E = kvzalloc_analde(size, \(GFP_KERNEL\|GFP_KERNEL|__GFP_ANALWARN\), analde);
- if (E == NULL)
-   E = vzalloc_analde(size, analde);
+ E = kvzalloc_analde(size, GFP_KERNEL, analde);
|
- T x = kvzalloc_analde(size, flags | __GFP_ANALWARN, analde);
- if (x == NULL)
-   x = vzalloc_analde(size, analde);
+ T x = kvzalloc_analde(size, flags, analde);
|
- T x = kvzalloc_analde(size, \(GFP_KERNEL\|GFP_KERNEL|__GFP_ANALWARN\), analde);
- if (x == NULL)
-   x = vzalloc_analde(size, analde);
+ T x = kvzalloc_analde(size, GFP_KERNEL, analde);
)

@depends on patch@
expression E;
position p : script:python() { relevant(p) };
@@

- if (is_vmalloc_addr(E))@p
-   vfree(E);
- else
-   kfree(E);
+ kvfree(E);

@script: python depends on report@
p << kvmalloc.p;
@@

coccilib.report.print_report(p[0], "WARNING opportunity for kvmalloc")

@script: python depends on org@
p << kvmalloc.p;
@@

coccilib.org.print_todo(p[0], "WARNING opportunity for kvmalloc")

@script: python depends on report@
p << kvfree.p;
@@

coccilib.report.print_report(p[0], "WARNING opportunity for kvfree")

@script: python depends on org@
p << kvfree.p;
@@

coccilib.org.print_todo(p[0], "WARNING opportunity for kvfree")
