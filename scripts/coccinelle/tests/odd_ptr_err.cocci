/// PTR_ERR should access the value just tested by IS_ERR
//# There can be false positives in the patch case, where it is the call
//# IS_ERR that is wrong.
///
// Confidence: High
// Copyright: (C) 2012 Julia Lawall, INRIA.  GPLv2.
// Copyright: (C) 2012 Gilles Muller, INRIA.  GPLv2.
// URL: http://coccinelle.lip6.fr/
// Comments:
// Options: --no-includes --include-headers

virtual patch
virtual context
virtual org
virtual report

@depends on patch@
expression e,e1;
@@

(
if (IS_ERR(e)) { ... PTR_ERR(e) ... }
|
if (IS_ERR(e=e1)) { ... PTR_ERR(e) ... }
|
if (IS_ERR(e))
 { ...
  PTR_ERR(
-   e1
+   e
  )
   ... }
)

@r depends on !patch@
expression e,e1;
position p1,p2;
@@

(
if (IS_ERR(e)) { ... PTR_ERR(e) ... }
|
if (IS_ERR(e=e1)) { ... PTR_ERR(e) ... }
|
*if (IS_ERR@p1(e))
 { ...
*  PTR_ERR@p2(e1)
   ... }
)

@script:python depends on org@
p1 << r.p1;
p2 << r.p2;
@@

cocci.print_main("inconsistent IS_ERR and PTR_ERR",p1)
cocci.print_secs("PTR_ERR",p2)

@script:python depends on report@
p1 << r.p1;
p2 << r.p2;
@@

msg = "inconsistent IS_ERR and PTR_ERR, PTR_ERR on line %s" % (p2[0].line)
coccilib.report.print_report(p1[0],msg)
