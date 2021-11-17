// SPDX-License-Identifier: GPL-2.0-only
/// Use memdup_user rather than duplicating its implementation
/// This is a little bit restricted to reduce false positives
///
// Confidence: High
// Copyright: (C) 2010-2012 Nicolas Palix.
// Copyright: (C) 2010-2012 Julia Lawall, INRIA/LIP6.
// Copyright: (C) 2010-2012 Gilles Muller, INRIA/LiP6.
// URL: http://coccinelle.lip6.fr/
// Comments:
// Options: --no-includes --include-headers

virtual patch
virtual context
virtual org
virtual report

@initialize:python@
@@
filter = frozenset(['memdup_user', 'vmemdup_user'])

def relevant(p):
    return not (filter & {el.current_element for el in p})

@depends on patch@
expression from,to,size;
identifier l1,l2;
position p : script:python() { relevant(p) };
@@

-  to = \(kmalloc@p\|kzalloc@p\)
-		(size,\(GFP_KERNEL\|GFP_USER\|
-		      \(GFP_KERNEL\|GFP_USER\)|__GFP_NOWARN\));
+  to = memdup_user(from,size);
   if (
-      to==NULL
+      IS_ERR(to)
                 || ...) {
   <+... when != goto l1;
-  -ENOMEM
+  PTR_ERR(to)
   ...+>
   }
-  if (copy_from_user(to, from, size) != 0) {
-    <+... when != goto l2;
-    -EFAULT
-    ...+>
-  }

@depends on patch@
expression from,to,size;
identifier l1,l2;
position p : script:python() { relevant(p) };
@@

-  to = \(kvmalloc@p\|kvzalloc@p\)(size,\(GFP_KERNEL\|GFP_USER\));
+  to = vmemdup_user(from,size);
   if (
-      to==NULL
+      IS_ERR(to)
                 || ...) {
   <+... when != goto l1;
-  -ENOMEM
+  PTR_ERR(to)
   ...+>
   }
-  if (copy_from_user(to, from, size) != 0) {
-    <+... when != goto l2;
-    -EFAULT
-    ...+>
-  }

@r depends on !patch@
expression from,to,size;
position p : script:python() { relevant(p) };
statement S1,S2;
@@

*  to = \(kmalloc@p\|kzalloc@p\)
		(size,\(GFP_KERNEL\|GFP_USER\|
		      \(GFP_KERNEL\|GFP_USER\)|__GFP_NOWARN\));
   if (to==NULL || ...) S1
   if (copy_from_user(to, from, size) != 0)
   S2

@rv depends on !patch@
expression from,to,size;
position p : script:python() { relevant(p) };
statement S1,S2;
@@

*  to = \(kvmalloc@p\|kvzalloc@p\)(size,\(GFP_KERNEL\|GFP_USER\));
   if (to==NULL || ...) S1
   if (copy_from_user(to, from, size) != 0)
   S2

@script:python depends on org@
p << r.p;
@@

coccilib.org.print_todo(p[0], "WARNING opportunity for memdup_user")

@script:python depends on report@
p << r.p;
@@

coccilib.report.print_report(p[0], "WARNING opportunity for memdup_user")

@script:python depends on org@
p << rv.p;
@@

coccilib.org.print_todo(p[0], "WARNING opportunity for vmemdup_user")

@script:python depends on report@
p << rv.p;
@@

coccilib.report.print_report(p[0], "WARNING opportunity for vmemdup_user")
