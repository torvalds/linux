// SPDX-License-Identifier: GPL-2.0
/// Make sure calls to d_find_alias() have a corresponding call to dput().
//
// Keywords: d_find_alias, dput
//
// Confidence: Moderate
// URL: https://coccinelle.gitlabpages.inria.fr/website
// Options: --include-headers

virtual context
virtual org
virtual patch
virtual report

@r exists@
local idexpression struct dentry *dent;
expression E, E1;
statement S1, S2;
position p1, p2;
@@
(
	if (!(dent@p1 = d_find_alias(...))) S1
|
	dent@p1 = d_find_alias(...)
)

<...when != dput(dent)
    when != if (...) { <+... dput(dent) ...+> }
    when != true !dent || ...
    when != dent = E
    when != E = dent
if (!dent || ...) S2
...>
(
	return <+...dent...+>;
|
	return @p2 ...;
|
	dent@p2 = E1;
|
	E1 = dent;
)

@depends on context@
local idexpression struct dentry *r.dent;
position r.p1,r.p2;
@@
* dent@p1 = ...
  ...
(
* return@p2 ...;
|
* dent@p2
)


@script:python depends on org@
p1 << r.p1;
p2 << r.p2;
@@
cocci.print_main("Missing call to dput()",p1)
cocci.print_secs("",p2)

@depends on patch@
local idexpression struct dentry *r.dent;
position r.p2;
@@
(
+ dput(dent);
  return @p2 ...;
|
+ dput(dent);
  dent@p2 = ...;
)

@script:python depends on report@
p1 << r.p1;
p2 << r.p2;
@@
msg = "Missing call to dput() at line %s."
coccilib.report.print_report(p1[0], msg % (p2[0].line))
