/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _XEN_EVENTS_H
#define _XEN_EVENTS_H

#include <linux/interrupt.h>
#include <linux/irq.h>
#ifdef CONFIG_PCI_MSI
#include <linux/msi.h>
#endif

#include <xen/interface/event_channel.h>
#include <asm/xen/hypercall.h>
#include <asm/xen/events.h>

struct xenbus_device;

unsigned xen_evtchn_nr_channels(void);

int bind_evtchn_to_irq(evtchn_port_t evtchn);
int bind_evtchn_to_irq_lateeoi(evtchn_port_t evtchn);
int bind_evtchn_to_irqhandler(evtchn_port_t evtchn,
			      irq_handler_t handler,
			      unsigned long irqflags, const char *devname,
			      void *dev_id);
int bind_evtchn_to_irqhandler_lateeoi(evtchn_port_t evtchn,
			      irq_handler_t handler,
			      unsigned long irqflags, const char *devname,
			      void *dev_id);
int bind_virq_to_irq(unsigned int virq, unsigned int cpu, bool percpu);
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
int bind_interdomain_evtchn_to_irq_lateeoi(struct xenbus_device *dev,
					   evtchn_port_t remote_port);
int bind_interdomain_evtchn_to_irqhandler_lateeoi(struct xenbus_device *dev,
						  evtchn_port_t remote_port,
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

/*
 * Send late EOI for an IRQ bound to an event channel via one of the *_lateeoi
 * functions above.
 */
void xen_irq_lateeoi(unsigned int irq, unsigned int eoi_flags);
/* Signal an event was spurious, i.e. there was no action resulting from it. */
#define XEN_EOI_FLAG_SPURIOUS	0x00000001

#define XEN_IRQ_PRIORITY_MAX     EVTCHN_FIFO_PRIORITY_MAX
#define XEN_IRQ_PRIORITY_DEFAULT EVTCHN_FIFO_PRIORITY_DEFAULT
#define XEN_IRQ_PRIORITY_MIN     EVTCHN_FIFO_PRIORITY_MIN
int xen_set_irq_priority(unsigned irq, unsigned priority);

/*
 * Allow extra references to event channels exposed to userspace by evtchn
 */
int evtchn_make_refcounted(evtchn_port_t evtchn, bool is_static);
int evtchn_get(evtchn_port_t evtchn);
void evtchn_put(evtchn_port_t evtchn);

void xen_send_IPI_one(unsigned int cpu, enum ipi_vector vector);
void rebind_evtchn_irq(evtchn_port_t evtchn, int irq);

static inline void notify_remote_via_evtchn(evtchn_port_t port)
{
	struct evtchn_send send = { .port = port };
	(void)HYPERVISOR_event_channel_op(EVTCHNOP_send, &send);
}

void notify_remote_via_irq(int irq);

void xen_irq_resume(void);

/* Clear an irq's pending state, in preparation for polling on it */
void xen_clear_irq_pending(int irq);
bool xen_test_irq_pending(int irq);

/* Poll waiting for an irq to become pending.  In the usual case, the
   irq will be disabled so it won't deliver an interrupt. */
void xen_poll_irq(int irq);

/* Poll waiting for an irq to become pending with a timeout.  In the usual case,
 * the irq will be disabled so it won't deliver an interrupt. */
void xen_poll_irq_timeout(int irq, u64 timeout);

/* Determine the IRQ which is bound to an event channel */
unsigned int irq_from_evtchn(evtchn_port_t evtchn);
int irq_evtchn_from_virq(unsigned int cpu, unsigned int virq,
			 evtchn_port_t *evtchn);

int xen_set_callback_via(uint64_t via);
int xen_evtchn_do_upcall(void);

/* Bind a pirq for a physical interrupt to an irq. */
int xen_bind_pirq_gsi_to_irq(unsigned gsi,
			     unsigned pirq, int shareable, char *name);

#ifdef CONFIG_PCI_MSI
/* Allocate a pirq for a MSI style physical interrupt. */
int xen_allocate_pirq_msi(struct pci_dev *dev, struct msi_desc *msidesc);
/* Bind an PSI pirq to an irq. */
int xen_bind_pirq_msi_to_irq(struct pci_dev *dev, struct msi_desc *msidesc,
			     int pirq, int nvec, const char *name, domid_t domid);
#endif

/* De-allocates the above mentioned physical interrupt. */
int xen_destroy_irq(int irq);

/* Return the pirq allocated to the irq. */
int xen_pirq_from_irq(unsigned irq);

/* Return the irq allocated to the gsi */
int xen_irq_from_gsi(unsigned gsi);

/* Determine whether to ignore this IRQ if it is passed to a guest. */
int xen_test_irq_shared(int irq);

/* initialize Xen IRQ subsystem */
void xen_init_IRQ(void);

irqreturn_t xen_debug_interrupt(int irq, void *dev_id);

static inline void xen_evtchn_close(evtchn_port_t port)
{
	struct evtchn_close close;

	close.port = port;
	if (HYPERVISOR_event_channel_op(EVTCHNOP_close, &close) != 0)
		BUG();
}

extern bool xen_fifo_events;

#endif	/* _XEN_EVENTS_H */
