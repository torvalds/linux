/// Simplify a trivial if-return sequence.  Possibly combine with a
/// preceding function call.
//
// Confidence: High
// Copyright: (C) 2014 Julia Lawall, INRIA/LIP6.  GPLv2.
// Copyright: (C) 2014 Gilles Muller, INRIA/LiP6.  GPLv2.
// URL: http://coccinelle.lip6.fr/
// Comments:
// Options: --no-includes --include-headers

virtual patch
virtual context
virtual org
virtual report

@r depends on patch@
local idexpression e;
identifier i,f,fn;
@@

fn(...) { <...
- e@i =
+ return
    f(...);
-if (i != 0) return i;
-return 0;
...> }

@depends on patch@
identifier r.i;
type t;
@@

-t i;
 ... when != i

@depends on patch@
expression e;
@@

-if (e != 0)
   return e;
-return 0;

// -----------------------------------------------------------------------

@s1 depends on context || org || report@
local idexpression e;
identifier i,f,fn;
position p,p1,p2;
@@

fn(...) { <...
* e@i@p = f(...);
  if (\(i@p1 != 0\|i@p2 < 0\))
     return i;
  return 0;
...> }

@s2 depends on context || org || report forall@
identifier s1.i;
type t;
position q,s1.p;
expression e,f;
@@

* t i@q;
  ... when != i
  e@p = f(...);

@s3 depends on context || org || report@
expression e;
position p1!=s1.p1;
position p2!=s1.p2;
@@

*if (\(e@p1 != 0\|e@p2 < 0\))
   return e;
 return 0;

// -----------------------------------------------------------------------

@script:python depends on org@
p << s1.p;
p1 << s1.p1;
q << s2.q;
@@

cocci.print_main("decl",q)
cocci.print_secs("use",p)
cocci.include_match(False)

@script:python depends on org@
p << s1.p;
p2 << s1.p2;
q << s2.q;
@@

cocci.print_main("decl",q)
cocci.print_secs("use with questionable test",p)
cocci.include_match(False)

@script:python depends on org@
p << s1.p;
p1 << s1.p1;
@@

cocci.print_main("use",p)

@script:python depends on org@
p << s1.p;
p2 << s1.p2;
@@

cocci.print_main("use with questionable test",p)

@script:python depends on org@
p << s3.p1;
@@

cocci.print_main("test",p)

@script:python depends on org@
p << s3.p2;
@@

cocci.print_main("questionable test",p)

// -----------------------------------------------------------------------

@script:python depends on report@
p << s1.p;
p1 << s1.p1;
q << s2.q;
@@

msg = "WARNING: end returns can be simpified and declaration on line %s can be dropped" % (q[0].line)
coccilib.report.print_report(p[0],msg)
cocci.include_match(False)

@script:python depends on report@
p << s1.p;
p1 << s1.p1;
q << s2.q
;
@@

msg = "WARNING: end returns may be simpified if negative or 0 value and declaration on line %s can be dropped" % (q[0].line)
coccilib.report.print_report(p[0],msg)
cocci.include_match(False)

@script:python depends on report@
p << s1.p;
p1 << s1.p1;
@@

msg = "WARNING: end returns can be simpified"
coccilib.report.print_report(p[0],msg)

@script:python depends on report@
p << s1.p;
p2 << s1.p2;
@@

msg = "WARNING: end returns can be simpified if negative or 0 value"
coccilib.report.print_report(p[0],msg)

@script:python depends on report@
p << s3.p1;
@@

msg = "WARNING: end returns can be simpified"
coccilib.report.print_report(p[0],msg)

@script:python depends on report@
p << s3.p2;
@@

msg = "WARNING: end returns can be simpified if tested value is negative or 0"
coccilib.report.print_report(p[0],msg)
