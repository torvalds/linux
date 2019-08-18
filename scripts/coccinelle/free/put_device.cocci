// SPDX-License-Identifier: GPL-2.0
/// Find missing put_device for every of_find_device_by_node.
///
// Confidence: Moderate
// Copyright: (C) 2018-2019 Wen Yang, ZTE.
// Comments:
// Options: --no-includes --include-headers

virtual report
virtual org

@search exists@
local idexpression id;
expression x,e,e1;
position p1,p2;
type T,T1,T2,T3;
@@

id = of_find_device_by_node@p1(x)
... when != e = id
if (id == NULL || ...) { ... return ...; }
... when != put_device(&id->dev)
    when != platform_device_put(id)
    when != of_dev_put(id)
    when != if (id) { ... put_device(&id->dev) ... }
    when != e1 = (T)id
    when != e1 = &id->dev
    when != e1 = get_device(&id->dev)
    when != e1 = (T1)platform_get_drvdata(id)
(
  return
(    id
|    (T2)dev_get_drvdata(&id->dev)
|    (T3)platform_get_drvdata(id)
|    &id->dev
);
| return@p2 ...;
)

@script:python depends on report@
p1 << search.p1;
p2 << search.p2;
@@

coccilib.report.print_report(p2[0], "ERROR: missing put_device; "
			      + "call of_find_device_by_node on line "
			      + p1[0].line
			      + ", but without a corresponding object release "
			      + "within this function.")

@script:python depends on org@
p1 << search.p1;
p2 << search.p2;
@@

cocci.print_main("of_find_device_by_node", p1)
cocci.print_secs("needed put_device", p2)
