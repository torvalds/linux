// SPDX-License-Identifier: GPL-2.0-only
///
/// Use resource_size function on resource object
/// instead of explicit computation.
///
//  Confidence: High
//  Copyright: (C) 2009, 2010 Nicolas Palix, DIKU.
//  Copyright: (C) 2009, 2010 Julia Lawall, DIKU.
//  Copyright: (C) 2009, 2010 Gilles Muller, INRIA/LiP6.
//  URL: https://coccinelle.gitlabpages.inria.fr/website
//  Options:
//
//  Keywords: resource_size
//  Version min: 2.6.27 resource_size
//

virtual context
virtual patch
virtual org
virtual report

//----------------------------------------------------------
//  For context mode
//----------------------------------------------------------

@r_context depends on context && !patch && !org@
struct resource *res;
@@

* (res->end - res->start) + 1

//----------------------------------------------------------
//  For patch mode
//----------------------------------------------------------

@r_patch depends on !context && patch && !org@
struct resource *res;
@@

- (res->end - res->start) + 1
+ resource_size(res)

//----------------------------------------------------------
//  For org mode
//----------------------------------------------------------


@r_org depends on !context && !patch && (org || report)@
struct resource *res;
position p;
@@

 (res->end@p - res->start) + 1

@rbad_org depends on !context && !patch && (org || report)@
struct resource *res;
position p != r_org.p;
@@

 res->end@p - res->start

@script:python depends on org@
p << r_org.p;
x << r_org.res;
@@

msg="ERROR with %s" % (x)
msg_safe=msg.replace("[","@(").replace("]",")")
coccilib.org.print_todo(p[0], msg_safe)

@script:python depends on report@
p << r_org.p;
x << r_org.res;
@@

msg="ERROR: Missing resource_size with %s" % (x)
coccilib.report.print_report(p[0], msg)

@script:python depends on org@
p << rbad_org.p;
x << rbad_org.res;
@@

msg="WARNING with %s" % (x)
msg_safe=msg.replace("[","@(").replace("]",")")
coccilib.org.print_todo(p[0], msg_safe)

@script:python depends on report@
p << rbad_org.p;
x << rbad_org.res;
@@

msg="WARNING: Suspicious code. resource_size is maybe missing with %s" % (x)
coccilib.report.print_report(p[0], msg)
