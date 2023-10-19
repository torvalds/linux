// SPDX-License-Identifier: GPL-2.0
/// Remove dev_err() messages after platform_get_irq*() failures
//
// Confidence: Medium
// Options: --include-headers

virtual patch
virtual context
virtual org
virtual report

@depends on context@
expression ret;
struct platform_device *E;
@@

ret =
(
platform_get_irq
|
platform_get_irq_byname
)(E, ...);

if ( \( ret < 0 \| ret <= 0 \) )
{
(
if (ret != -EPROBE_DEFER)
{ ...
*dev_err(...);
... }
|
...
*dev_err(...);
)
...
}

@depends on patch@
expression ret;
struct platform_device *E;
@@

ret =
(
platform_get_irq
|
platform_get_irq_byname
)(E, ...);

if ( \( ret < 0 \| ret <= 0 \) )
{
(
-if (ret != -EPROBE_DEFER)
-{ ...
-dev_err(...);
-... }
|
...
-dev_err(...);
)
...
}

@r depends on org || report@
position p1;
expression ret;
struct platform_device *E;
@@

ret =
(
platform_get_irq
|
platform_get_irq_byname
)(E, ...);

if ( \( ret < 0 \| ret <= 0 \) )
{
(
if (ret != -EPROBE_DEFER)
{ ...
dev_err@p1(...);
... }
|
...
dev_err@p1(...);
)
...
}

@script:python depends on org@
p1 << r.p1;
@@

cocci.print_main(p1)

@script:python depends on report@
p1 << r.p1;
@@

msg = "line %s is redundant because platform_get_irq() already prints an error" % (p1[0].line)
coccilib.report.print_report(p1[0],msg)
