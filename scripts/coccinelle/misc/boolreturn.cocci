// SPDX-License-Identifier: GPL-2.0
/// Return statements in functions returning bool should use
/// true/false instead of 1/0.
//
// Confidence: High
// Options: --no-includes --include-headers

virtual patch
virtual report
virtual context

@r1 depends on patch@
identifier fn;
typedef bool;
symbol false;
symbol true;
@@

bool fn ( ... )
{
<...
return
(
- 0
+ false
|
- 1
+ true
)
  ;
...>
}

@r2 depends on report || context@
identifier fn;
position p;
@@

bool fn ( ... )
{
<...
return
(
* 0@p
|
* 1@p
)
  ;
...>
}


@script:python depends on report@
p << r2.p;
fn << r2.fn;
@@

msg = "WARNING: return of 0/1 in function '%s' with return type bool" % fn
coccilib.report.print_report(p[0], msg)
