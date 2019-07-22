// SPDX-License-Identifier: GPL-2.0-only
///
/// Remove unneeded semicolon.
///
// Confidence: Moderate
// Copyright: (C) 2012 Peter Senna Tschudin, INRIA/LIP6.
// URL: http://coccinelle.lip6.fr/
// Comments: Some false positives on empty default cases in switch statements.
// Options: --no-includes --include-headers

virtual patch
virtual report
virtual context
virtual org

@r_default@
position p;
@@
switch (...)
{
default: ...;@p
}

@r_case@
position p;
@@
(
switch (...)
{
case ...:;@p
}
|
switch (...)
{
case ...:...
case ...:;@p
}
|
switch (...)
{
case ...:...
case ...:
case ...:;@p
}
)

@r1@
statement S;
position p1;
position p != {r_default.p, r_case.p};
identifier label;
@@
(
label:;
|
S@p1;@p
)

@script:python@
p << r1.p;
p1 << r1.p1;
@@
if p[0].line != p1[0].line_end:
	cocci.include_match(False)

@depends on patch@
position r1.p;
@@
-;@p

@script:python depends on report@
p << r1.p;
@@
coccilib.report.print_report(p[0],"Unneeded semicolon")

@depends on context@
position r1.p;
@@
*;@p

@script:python depends on org@
p << r1.p;
@@
cocci.print_main("Unneeded semicolon",p)
