/// Make sure threaded IRQs without a primary handler are always request with
/// IRQF_ONESHOT
///
//
// Confidence: Good
// Comments:
// Options: --no-includes

virtual patch
virtual context
virtual org
virtual report

@r1@
expression dev;
expression irq;
expression thread_fn;
expression flags;
position p;
@@
(
request_threaded_irq@p(irq, NULL, thread_fn,
(
flags | IRQF_ONESHOT
|
IRQF_ONESHOT
)
, ...)
|
devm_request_threaded_irq@p(dev, irq, NULL, thread_fn,
(
flags | IRQF_ONESHOT
|
IRQF_ONESHOT
)
, ...)
)

@depends on patch@
expression dev;
expression irq;
expression thread_fn;
expression flags;
position p != r1.p;
@@
(
request_threaded_irq@p(irq, NULL, thread_fn,
(
-0
+IRQF_ONESHOT
|
-flags
+flags | IRQF_ONESHOT
)
, ...)
|
devm_request_threaded_irq@p(dev, irq, NULL, thread_fn,
(
-0
+IRQF_ONESHOT
|
-flags
+flags | IRQF_ONESHOT
)
, ...)
)

@depends on context@
position p != r1.p;
@@
*request_threaded_irq@p(...)

@match depends on report || org@
expression irq;
position p != r1.p;
@@
request_threaded_irq@p(irq, NULL, ...)

@script:python depends on org@
p << match.p;
@@
msg = "ERROR: Threaded IRQ with no primary handler requested without IRQF_ONESHOT"
coccilib.org.print_todo(p[0],msg)

@script:python depends on report@
p << match.p;
@@
msg = "ERROR: Threaded IRQ with no primary handler requested without IRQF_ONESHOT"
coccilib.report.print_report(p[0],msg)
