///
/// Use kzalloc rather than kmalloc followed by memset with 0
///
/// This considers some simple cases that are common and easy to validate
/// Note in particular that there are no ...s in the rule, so all of the
/// matched code has to be contiguous
///
// Confidence: High
// Copyright: (C) 2009-2010 Julia Lawall, Nicolas Palix, DIKU.  GPLv2.
// Copyright: (C) 2009-2010 Gilles Muller, INRIA/LiP6.  GPLv2.
// URL: http://coccinelle.lip6.fr/rules/kzalloc.html
// Options: --no-includes --include-headers
//
// Keywords: kmalloc, kzalloc
// Version min: < 2.6.12 kmalloc
// Version min:   2.6.14 kzalloc
//

virtual context
virtual patch
virtual org
virtual report

//----------------------------------------------------------
//  For context mode
//----------------------------------------------------------

@depends on context@
type T, T2;
expression x;
expression E1,E2;
statement S;
@@

* x = (T)kmalloc(E1,E2);
  if ((x==NULL) || ...) S
* memset((T2)x,0,E1);

//----------------------------------------------------------
//  For patch mode
//----------------------------------------------------------

@depends on patch@
type T, T2;
expression x;
expression E1,E2;
statement S;
@@

- x = (T)kmalloc(E1,E2);
+ x = kzalloc(E1,E2);
  if ((x==NULL) || ...) S
- memset((T2)x,0,E1);

//----------------------------------------------------------
//  For org mode
//----------------------------------------------------------

@r depends on org || report@
type T, T2;
expression x;
expression E1,E2;
statement S;
position p;
@@

 x = (T)kmalloc@p(E1,E2);
 if ((x==NULL) || ...) S
 memset((T2)x,0,E1);

@script:python depends on org@
p << r.p;
x << r.x;
@@

msg="%s" % (x)
msg_safe=msg.replace("[","@(").replace("]",")")
coccilib.org.print_todo(p[0], msg_safe)

@script:python depends on report@
p << r.p;
x << r.x;
@@

msg="WARNING: kzalloc should be used for %s, instead of kmalloc/memset" % (x)
coccilib.report.print_report(p[0], msg)
