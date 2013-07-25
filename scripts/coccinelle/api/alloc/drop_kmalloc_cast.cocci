///
/// Casting (void *) value returned by kmalloc is useless
/// as mentioned in Documentation/CodingStyle, Chap 14.
///
// Confidence: High
// Copyright: 2009,2010 Nicolas Palix, DIKU.  GPLv2.
// URL: http://coccinelle.lip6.fr/
// Options: --no-includes --include-headers
//
// Keywords: kmalloc, kzalloc, kcalloc
// Version min: < 2.6.12 kmalloc
// Version min: < 2.6.12 kcalloc
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
type T;
@@

* (T *)
  \(kmalloc\|kzalloc\|kcalloc\)(...)

//----------------------------------------------------------
//  For patch mode
//----------------------------------------------------------

@depends on patch@
type T;
@@

- (T *)
  \(kmalloc\|kzalloc\|kcalloc\)(...)

//----------------------------------------------------------
//  For org and report mode
//----------------------------------------------------------

@r depends on org || report@
type T;
position p;
@@

 (T@p *)\(kmalloc\|kzalloc\|kcalloc\)(...)

@script:python depends on org@
p << r.p;
t << r.T;
@@

coccilib.org.print_safe_todo(p[0], t)

@script:python depends on report@
p << r.p;
t << r.T;
@@

msg="WARNING: casting value returned by k[cmz]alloc to (%s *) is useless." % (t)
coccilib.report.print_report(p[0], msg)
