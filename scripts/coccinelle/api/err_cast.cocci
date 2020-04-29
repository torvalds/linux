// SPDX-License-Identifier: GPL-2.0-only
///
/// Use ERR_CAST inlined function instead of ERR_PTR(PTR_ERR(...))
///
// Confidence: High
// Copyright: (C) 2009, 2010 Nicolas Palix, DIKU.
// Copyright: (C) 2009, 2010 Julia Lawall, DIKU.
// Copyright: (C) 2009, 2010 Gilles Muller, INRIA/LiP6.
// URL: http://coccinelle.lip6.fr/
// Options:
//
// Keywords: ERR_PTR, PTR_ERR, ERR_CAST
// Version min: 2.6.25
//

virtual context
virtual patch
virtual org
virtual report


@ depends on context && !patch && !org && !report@
expression x;
@@

* ERR_PTR(PTR_ERR(x))

@ depends on !context && patch && !org && !report @
expression x;
@@

- ERR_PTR(PTR_ERR(x))
+ ERR_CAST(x)

@r depends on !context && !patch && (org || report)@
expression x;
position p;
@@

 ERR_PTR@p(PTR_ERR(x))

@script:python depends on org@
p << r.p;
x << r.x;
@@

msg="WARNING ERR_CAST can be used with %s" % (x)
msg_safe=msg.replace("[","@(").replace("]",")")
coccilib.org.print_todo(p[0], msg_safe)

@script:python depends on report@
p << r.p;
x << r.x;
@@

msg="WARNING: ERR_CAST can be used with %s" % (x)
coccilib.report.print_report(p[0], msg)
