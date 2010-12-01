#ifndef _XEN_EVENTS_H
#define _XEN_EVENTS_H

#include <linux/interrupt.h>

#include <xen/interface/event_channel.h>
#include <asm/xen/hypercall.h>
#include <asm/xen/events.h>

int bind_evtchn_to_irq(unsigned int evtchn);
int bind_evtchn_to_irqhandler(unsigned int evtchn,
			      irq_handler_t handler,
			      unsigned long irqflags, const char *devname,
			      void *dev_id);
int bind_virq_to_irq(unsigned int virq, unsigned int cpu);
int bind_virq_to_irqhandler(unsigned int virq, unsigned int cpu,
			    irq_handler_t handler,
			    unsigned long irqflags, const char *devname,
			    void *dev_id);
int bind_ipi_to_irqhandler(enum ipi_vector ipi,
			   unsigned int cpu,
			   irq_handler_t handler,
			   unsigned long irqflags,
			   const char *devname,
			   void *dev_id);

/*
 * Common unbind function for all event sources. Takes IRQ to unbind from.
 * Automatically closes the underlying event channel (even for bindings
 * made with bind_evtchn_to_irqhandler()).
 */
void unbind_from_irqhandler(unsigned int irq, void *dev_id);

void xen_send_IPI_one(unsigned int cpu, enum ipi_vector vector);
int resend_irq_on_evtchn(unsigned int irq);
void rebind_evtchn_irq(int evtchn, int irq);

static inline void notify_remote_via_evtchn(int port)
{
	struct evtchn_send send = { .port = port };
	(void)HYPERVISOR_event_channel_op(EVTCHNOP_send, &send);
}

extern void notify_remote_via_irq(int irq);

extern void xen_irq_resume(void);

/* Clear an irq's pending state, in preparation for polling on it */
void xen_clear_irq_pending(int irq);
void xen_set_irq_pending(int irq);
bool xen_test_irq_pending(int irq);

/* Poll waiting for an irq to become pending.  In the usual case, the
   irq will be disabled so it won't deliver an interrupt. */
void xen_poll_irq(int irq);

/* Poll waiting for an irq to become pending with a timeout.  In the usual case,
 * the irq will be disabled so it won't deliver an interrupt. */
void xen_poll_irq_timeout(int irq, u64 timeout);

/* Determine the IRQ which is bound to an event channel */
unsigned irq_from_evtchn(unsigned int evtchn);

/* Xen HVM evtchn vector callback */
extern void xen_hvm_callback_vector(void);
extern int xen_have_vector_callback;
int xen_set_callback_via(uint64_t via);
void xen_evtchn_do_upcall(struct pt_regs *regs);
void xen_hvm_evtchn_do_upcall(void);

/* Allocate an irq for a physical interrupt, given a gsi.  "Legacy"
 * GSIs are identity mapped; others are dynamically allocated as
 * usual. */
int xen_allocate_pirq(unsigned gsi, int shareable, char *name);
int xen_map_pirq_gsi(unsigned pirq, unsigned gsi, int shareable, char *name);

#ifdef CONFIG_PCI_MSI
/* Allocate an irq and a pirq to be used with MSIs. */
#define XEN_ALLOC_PIRQ (1 << 0)
#define XEN_ALLOC_IRQ  (1 << 1)
void xen_allocate_pirq_msi(char *name, int *irq, int *pirq, int alloc_mask);
int xen_create_msi_irq(struct pci_dev *dev, struct msi_desc *msidesc, int type);
#endif

/* De-allocates the above mentioned physical interrupt. */
int xen_destroy_irq(int irq);

/* Return vector allocated to pirq */
int xen_vector_from_irq(unsigned pirq);

/* Return gsi allocated to pirq */
int xen_gsi_from_irq(unsigned pirq);

/* Return irq from pirq */
int xen_irq_from_pirq(unsigned pirq);

#endif	/* _XEN_EVENTS_H */
