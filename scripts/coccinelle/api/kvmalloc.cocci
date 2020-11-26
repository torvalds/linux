// SPDX-License-Identifier: GPL-2.0-only
///
/// Find if/else condition with kmalloc/vmalloc calls.
/// Suggest to use kvmalloc instead. Same for kvfree.
///
// Confidence: High
// Copyright: (C) 2020 Denis Efremov ISPRAS
// Options: --no-includes --include-headers
//

virtual patch
virtual report
virtual org
virtual context

@initialize:python@
@@
filter = frozenset(['kvfree'])

def relevant(p):
    return not (filter & {el.current_element for el in p})

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
*    E = \(kmalloc\|kzalloc\|kcalloc\|kmalloc_node\|kzalloc_node\|
*          kmalloc_array\|kmalloc_array_node\|kcalloc_node\)
*          (..., size, \(flags\|GFP_KERNEL\|\(GFP_KERNEL\|flags\)|__GFP_NOWARN\), ...)
    ...
  } else {
    ...
*    E = \(vmalloc\|vzalloc\|vmalloc_node\|vzalloc_node\)(..., size, ...)
    ...
  }
|
* E = \(kmalloc\|kzalloc\|kcalloc\|kmalloc_node\|kzalloc_node\|
*       kmalloc_array\|kmalloc_array_node\|kcalloc_node\)
*       (..., size, \(flags\|GFP_KERNEL\|\(GFP_KERNEL\|flags\)|__GFP_NOWARN\), ...)
  ... when != E = E1
      when != size = E1
      when any
* if (E == NULL)@p {
    ...
*   E = \(vmalloc\|vzalloc\|vmalloc_node\|vzalloc_node\)(..., size, ...)
    ...
  }
|
* T x = \(kmalloc\|kzalloc\|kcalloc\|kmalloc_node\|kzalloc_node\|
*         kmalloc_array\|kmalloc_array_node\|kcalloc_node\)
*         (..., size, \(flags\|GFP_KERNEL\|\(GFP_KERNEL\|flags\)|__GFP_NOWARN\), ...);
  ... when != x = E1
      when != size = E1
      when any
* if (x == NULL)@p {
    ...
*   x = \(vmalloc\|vzalloc\|vmalloc_node\|vzalloc_node\)(..., size, ...)
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
*   \(kfree\|kzfree\)(E)
    ...
  }

@depends on patch@
expression E, E1, size, node;
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
-    E = kmalloc(size, \(GFP_KERNEL\|GFP_KERNEL|__GFP_NOWARN\));
- else
-    E = vmalloc(size);
+ E = kvmalloc(size, GFP_KERNEL);
|
- E = kmalloc(size, flags | __GFP_NOWARN);
- if (E == NULL)
-   E = vmalloc(size);
+ E = kvmalloc(size, flags);
|
- E = kmalloc(size, \(GFP_KERNEL\|GFP_KERNEL|__GFP_NOWARN\));
- if (E == NULL)
-   E = vmalloc(size);
+ E = kvmalloc(size, GFP_KERNEL);
|
- T x = kmalloc(size, flags | __GFP_NOWARN);
- if (x == NULL)
-   x = vmalloc(size);
+ T x = kvmalloc(size, flags);
|
- T x = kmalloc(size, \(GFP_KERNEL\|GFP_KERNEL|__GFP_NOWARN\));
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
-    E = kzalloc(size, \(GFP_KERNEL\|GFP_KERNEL|__GFP_NOWARN\));
- else
-    E = vzalloc(size);
+ E = kvzalloc(size, GFP_KERNEL);
|
- E = kzalloc(size, flags | __GFP_NOWARN);
- if (E == NULL)
-   E = vzalloc(size);
+ E = kvzalloc(size, flags);
|
- E = kzalloc(size, \(GFP_KERNEL\|GFP_KERNEL|__GFP_NOWARN\));
- if (E == NULL)
-   E = vzalloc(size);
+ E = kvzalloc(size, GFP_KERNEL);
|
- T x = kzalloc(size, flags | __GFP_NOWARN);
- if (x == NULL)
-   x = vzalloc(size);
+ T x = kvzalloc(size, flags);
|
- T x = kzalloc(size, \(GFP_KERNEL\|GFP_KERNEL|__GFP_NOWARN\));
- if (x == NULL)
-   x = vzalloc(size);
+ T x = kvzalloc(size, GFP_KERNEL);
|
- if (size cmp E1)
-    E = kmalloc_node(size, flags, node);
- else
-    E = vmalloc_node(size, node);
+ E = kvmalloc_node(size, flags, node);
|
- if (size cmp E1)
-    E = kmalloc_node(size, \(GFP_KERNEL\|GFP_KERNEL|__GFP_NOWARN\), node);
- else
-    E = vmalloc_node(size, node);
+ E = kvmalloc_node(size, GFP_KERNEL, node);
|
- E = kmalloc_node(size, flags | __GFP_NOWARN, node);
- if (E == NULL)
-   E = vmalloc_node(size, node);
+ E = kvmalloc_node(size, flags, node);
|
- E = kmalloc_node(size, \(GFP_KERNEL\|GFP_KERNEL|__GFP_NOWARN\), node);
- if (E == NULL)
-   E = vmalloc_node(size, node);
+ E = kvmalloc_node(size, GFP_KERNEL, node);
|
- T x = kmalloc_node(size, flags | __GFP_NOWARN, node);
- if (x == NULL)
-   x = vmalloc_node(size, node);
+ T x = kvmalloc_node(size, flags, node);
|
- T x = kmalloc_node(size, \(GFP_KERNEL\|GFP_KERNEL|__GFP_NOWARN\), node);
- if (x == NULL)
-   x = vmalloc_node(size, node);
+ T x = kvmalloc_node(size, GFP_KERNEL, node);
|
- if (size cmp E1)
-    E = kvzalloc_node(size, flags, node);
- else
-    E = vzalloc_node(size, node);
+ E = kvzalloc_node(size, flags, node);
|
- if (size cmp E1)
-    E = kvzalloc_node(size, \(GFP_KERNEL\|GFP_KERNEL|__GFP_NOWARN\), node);
- else
-    E = vzalloc_node(size, node);
+ E = kvzalloc_node(size, GFP_KERNEL, node);
|
- E = kvzalloc_node(size, flags | __GFP_NOWARN, node);
- if (E == NULL)
-   E = vzalloc_node(size, node);
+ E = kvzalloc_node(size, flags, node);
|
- E = kvzalloc_node(size, \(GFP_KERNEL\|GFP_KERNEL|__GFP_NOWARN\), node);
- if (E == NULL)
-   E = vzalloc_node(size, node);
+ E = kvzalloc_node(size, GFP_KERNEL, node);
|
- T x = kvzalloc_node(size, flags | __GFP_NOWARN, node);
- if (x == NULL)
-   x = vzalloc_node(size, node);
+ T x = kvzalloc_node(size, flags, node);
|
- T x = kvzalloc_node(size, \(GFP_KERNEL\|GFP_KERNEL|__GFP_NOWARN\), node);
- if (x == NULL)
-   x = vzalloc_node(size, node);
+ T x = kvzalloc_node(size, GFP_KERNEL, node);
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
