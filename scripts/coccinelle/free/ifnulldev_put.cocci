// SPDX-License-Identifier: GPL-2.0-only
/// Since commit b37a46683739 ("netdevice: add the case if dev is NULL"),
/// NULL check before dev_{put, hold} functions is not needed.
///
/// Based on ifnullfree.cocci by Fabian Frederick.
///
// Copyright: (C) 2022 Ziyang Xuan.
// Comments: -
// Options: --no-includes --include-headers
// Version min: 5.15

virtual patch
virtual org
virtual report
virtual context

@r2 depends on patch@
expression E;
@@
- if (E != NULL)
(
  __dev_put(E);
|
  dev_put(E);
|
  dev_put_track(E, ...);
|
  __dev_hold(E);
|
  dev_hold(E);
|
  dev_hold_track(E, ...);
)

@r depends on context || report || org @
expression E;
position p;
@@

* if (E != NULL)
*	\(__dev_put@p\|dev_put@p\|dev_put_track@p\|__dev_hold@p\|dev_hold@p\|
*         dev_hold_track@p\)(E, ...);

@script:python depends on org@
p << r.p;
@@

cocci.print_main("NULL check before dev_{put, hold} functions is not needed", p)

@script:python depends on report@
p << r.p;
@@

msg = "WARNING: NULL check before dev_{put, hold} functions is not needed."
coccilib.report.print_report(p[0], msg)
