// SPDX-License-Identifier: GPL-2.0
/// Use devm_platform_ioremap_resource helper which wraps
/// platform_get_resource() and devm_ioremap_resource() together.
///
// Confidence: High
// Copyright: (C) 2019 Himanshu Jha GPLv2.
// Copyright: (C) 2019 Julia Lawall, Inria/LIP6. GPLv2.
// Keywords: platform_get_resource, devm_ioremap_resource,
// Keywords: devm_platform_ioremap_resource

virtual patch
virtual report

@r depends on patch && !report@
expression e1, e2, arg1, arg2, arg3;
identifier id;
@@

(
- id = platform_get_resource(arg1, IORESOURCE_MEM, arg2);
|
- struct resource *id = platform_get_resource(arg1, IORESOURCE_MEM, arg2);
)
  ... when != id
- e1 = devm_ioremap_resource(arg3, id);
+ e1 = devm_platform_ioremap_resource(arg1, arg2);
  ... when != id
? id = e2

@r1 depends on patch && !report@
identifier r.id;
type T;
@@

- T *id;
  ...when != id

@r2 depends on report && !patch@
identifier id;
expression e1, e2, arg1, arg2, arg3;
position j0;
@@

(
  id = platform_get_resource(arg1, IORESOURCE_MEM, arg2);
|
  struct resource *id = platform_get_resource(arg1, IORESOURCE_MEM, arg2);
)
  ... when != id
  e1@j0 = devm_ioremap_resource(arg3, id);
  ... when != id
? id = e2

@script:python depends on report && !patch@
e1 << r2.e1;
j0 << r2.j0;
@@

msg = "WARNING: Use devm_platform_ioremap_resource for %s" % (e1)
coccilib.report.print_report(j0[0], msg)
