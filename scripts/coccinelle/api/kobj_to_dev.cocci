// SPDX-License-Identifier: GPL-2.0-only
///
/// Use kobj_to_dev() instead of container_of()
///
// Confidence: High
// Copyright: (C) 2020 Denis Efremov ISPRAS
// Options: --no-includes --include-headers
//
// Keywords: kobj_to_dev, container_of
//

virtual context
virtual report
virtual org
virtual patch


@r depends on !patch@
expression ptr;
symbol kobj;
position p;
@@

* container_of(ptr, struct device, kobj)@p


@depends on patch@
expression ptr;
@@

- container_of(ptr, struct device, kobj)
+ kobj_to_dev(ptr)


@script:python depends on report@
p << r.p;
@@

coccilib.report.print_report(p[0], "WARNING opportunity for kobj_to_dev()")

@script:python depends on org@
p << r.p;
@@

coccilib.org.print_todo(p[0], "WARNING opportunity for kobj_to_dev()")
