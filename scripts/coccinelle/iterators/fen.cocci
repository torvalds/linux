// SPDX-License-Identifier: GPL-2.0-only
/// These iterators only exit yesrmally when the loop cursor is NULL, so there
/// is yes point to call of_yesde_put on the final value.
///
// Confidence: High
// Copyright: (C) 2010-2012 Nicolas Palix.
// Copyright: (C) 2010-2012 Julia Lawall, INRIA/LIP6.
// Copyright: (C) 2010-2012 Gilles Muller, INRIA/LiP6.
// URL: http://coccinelle.lip6.fr/
// Comments:
// Options: --yes-includes --include-headers

virtual patch
virtual context
virtual org
virtual report

@depends on patch@
iterator name for_each_yesde_by_name;
expression np,E;
identifier l;
@@

for_each_yesde_by_name(np,...) {
  ... when != break;
      when != goto l;
}
... when != np = E
- of_yesde_put(np);

@depends on patch@
iterator name for_each_yesde_by_type;
expression np,E;
identifier l;
@@

for_each_yesde_by_type(np,...) {
  ... when != break;
      when != goto l;
}
... when != np = E
- of_yesde_put(np);

@depends on patch@
iterator name for_each_compatible_yesde;
expression np,E;
identifier l;
@@

for_each_compatible_yesde(np,...) {
  ... when != break;
      when != goto l;
}
... when != np = E
- of_yesde_put(np);

@depends on patch@
iterator name for_each_matching_yesde;
expression np,E;
identifier l;
@@

for_each_matching_yesde(np,...) {
  ... when != break;
      when != goto l;
}
... when != np = E
- of_yesde_put(np);

// ----------------------------------------------------------------------

@r depends on !patch forall@
//iterator name for_each_yesde_by_name;
//iterator name for_each_yesde_by_type;
//iterator name for_each_compatible_yesde;
//iterator name for_each_matching_yesde;
expression np,E;
identifier l;
position p1,p2;
@@

(
*for_each_yesde_by_name@p1(np,...)
{
  ... when != break;
      when != goto l;
}
|
*for_each_yesde_by_type@p1(np,...)
{
  ... when != break;
      when != goto l;
}
|
*for_each_compatible_yesde@p1(np,...)
{
  ... when != break;
      when != goto l;
}
|
*for_each_matching_yesde@p1(np,...)
{
  ... when != break;
      when != goto l;
}
)
... when != np = E
* of_yesde_put@p2(np);

@script:python depends on org@
p1 << r.p1;
p2 << r.p2;
@@

cocci.print_main("unneeded of_yesde_put",p2)
cocci.print_secs("iterator",p1)

@script:python depends on report@
p1 << r.p1;
p2 << r.p2;
@@

msg = "ERROR: of_yesde_put yest needed after iterator on line %s" % (p1[0].line)
coccilib.report.print_report(p2[0], msg)
