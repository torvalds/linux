#ifndef _XEN_EVENTS_H
#define _XEN_EVENTS_H

#include <linux/irq.h>

int bind_evtchn_to_irqhandler(unsigned int evtchn,
			      irqreturn_t (*handler)(int, void *),
			      unsigned long irqflags, const char *devname,
			      void *dev_id);
int bind_virq_to_irqhandler(unsigned int virq, unsigned int cpu,
			    irqreturn_t (*handler)(int, void *),
			    unsigned long irqflags, const char *devname, void *dev_id);

/*
 * Common unbind function for all event sources. Takes IRQ to unbind from.
 * Automatically closes the underlying event channel (even for bindings
 * made with bind_evtchn_to_irqhandler()).
 */
void unbind_from_irqhandler(unsigned int irq, void *dev_id);

static inline void notify_remote_via_evtchn(int port)
{
	struct evtchn_send send = { .port = port };
	(void)HYPERVISOR_event_channel_op(EVTCHNOP_send, &send);
}

extern void notify_remote_via_irq(int irq);
#endif	/* _XEN_EVENTS_H */
