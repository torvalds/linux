/* $Id: pil.h,v 1.1 2002/01/23 11:27:36 davem Exp $ */
#ifndef _SPARC64_PIL_H
#define _SPARC64_PIL_H

/* To avoid some locking problems, we hard allocate certain PILs
 * for SMP cross call messages that must do a etrap/rtrap.
 *
 * A local_irq_disable() does not block the cross call delivery, so
 * when SMP locking is an issue we reschedule the event into a PIL
 * interrupt which is blocked by local_irq_disable().
 *
 * In fact any XCALL which has to etrap/rtrap has a problem because
 * it is difficult to prevent rtrap from running BH's, and that would
 * need to be done if the XCALL arrived while %pil==15.
 */
#define PIL_SMP_CALL_FUNC	1
#define PIL_SMP_RECEIVE_SIGNAL	2
#define PIL_SMP_CAPTURE		3
#define PIL_SMP_CTX_NEW_VERSION	4
#define PIL_DEVICE_IRQ		5

#ifndef __ASSEMBLY__
#define PIL_RESERVED(PIL)	((PIL) == PIL_SMP_CALL_FUNC || \
				 (PIL) == PIL_SMP_RECEIVE_SIGNAL || \
				 (PIL) == PIL_SMP_CAPTURE || \
				 (PIL) == PIL_SMP_CTX_NEW_VERSION)
#endif

#endif /* !(_SPARC64_PIL_H) */
