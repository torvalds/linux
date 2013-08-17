/// Find duplicate field initializations.  This has a high rate of false
/// positives due to #ifdefs, which Coccinelle is not aware of in a structure
/// initialization.
///
// Confidence: Low
// Copyright: (C) 2010-2012 Nicolas Palix.  GPLv2.
// Copyright: (C) 2010-2012 Julia Lawall, INRIA/LIP6.  GPLv2.
// Copyright: (C) 2010-2012 Gilles Muller, INRIA/LiP6.  GPLv2.
// URL: http://coccinelle.lip6.fr/
// Comments: requires at least Coccinelle 0.2.4, lex or parse error otherwise
// Options: -no_includes -include_headers

virtual org
virtual report

@r@
identifier I, s, fld;
position p0,p;
expression E;
@@

struct I s =@p0 { ..., .fld@p = E, ...};

@s@
identifier I, s, r.fld;
position r.p0,p;
expression E;
@@

struct I s =@p0 { ..., .fld@p = E, ...};

@script:python depends on org@
p0 << r.p0;
fld << r.fld;
ps << s.p;
pr << r.p;
@@

if int(ps[0].line) < int(pr[0].line) or (int(ps[0].line) == int(pr[0].line) and int(ps[0].column) < int(pr[0].column)):
  cocci.print_main(fld,p0)
  cocci.print_secs("s",ps)
  cocci.print_secs("r",pr)

@script:python depends on report@
p0 << r.p0;
fld << r.fld;
ps << s.p;
pr << r.p;
@@

if int(ps[0].line) < int(pr[0].line) or (int(ps[0].line) == int(pr[0].line) and int(ps[0].column) < int(pr[0].column)):
  msg = "%s: first occurrence line %s, second occurrence line %s" % (fld,ps[0].line,pr[0].line)
  coccilib.report.print_report(p0[0],msg)
