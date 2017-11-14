// SPDX-License-Identifier: GPL-2.0
/// Since commit 1c6c69525b40 ("genirq: Reject bogus threaded irq requests")
/// threaded IRQs without a primary handler need to be requested with
/// IRQF_ONESHOT, otherwise the request will fail.
///
/// So pass the IRQF_ONESHOT flag in this case.
///
//
// Confidence: Moderate
// Comments:
// Options: --no-includes

virtual patch
virtual context
virtual org
virtual report

@r1@
expression dev, irq, thread_fn;
position p;
@@
(
request_threaded_irq@p(irq, NULL, thread_fn,
(
IRQF_ONESHOT | ...
|
IRQF_ONESHOT
)
, ...)
|
devm_request_threaded_irq@p(dev, irq, NULL, thread_fn,
(
IRQF_ONESHOT | ...
|
IRQF_ONESHOT
)
, ...)
)

@r2@
expression dev, irq, thread_fn, flags, e;
position p != r1.p;
@@
(
flags = IRQF_ONESHOT | ...
|
flags |= IRQF_ONESHOT | ...
)
... when != flags = e
(
request_threaded_irq@p(irq, NULL, thread_fn, flags, ...);
|
devm_request_threaded_irq@p(dev, irq, NULL, thread_fn, flags, ...);
)

@depends on patch@
expression dev, irq, thread_fn, flags;
position p != {r1.p,r2.p};
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
expression dev, irq;
position p != {r1.p,r2.p};
@@
(
*request_threaded_irq@p(irq, NULL, ...)
|
*devm_request_threaded_irq@p(dev, irq, NULL, ...)
)


@match depends on report || org@
expression dev, irq;
position p != {r1.p,r2.p};
@@
(
request_threaded_irq@p(irq, NULL, ...)
|
devm_request_threaded_irq@p(dev, irq, NULL, ...)
)

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
