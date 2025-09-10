/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 * irqfd: Allows an fd to be used to inject an interrupt to the guest
 * Credit goes to Avi Kivity for the original idea.
 */

#ifndef __LINUX_KVM_IRQFD_H
#define __LINUX_KVM_IRQFD_H

#include <linux/kvm_host.h>
#include <linux/poll.h>

/*
 * Resampling irqfds are a special variety of irqfds used to emulate
 * level triggered interrupts.  The interrupt is asserted on eventfd
 * trigger.  On acknowledgment through the irq ack notifier, the
 * interrupt is de-asserted and userspace is notified through the
 * resamplefd.  All resamplers on the same gsi are de-asserted
 * together, so we don't need to track the state of each individual
 * user.  We can also therefore share the same irq source ID.
 */
struct kvm_kernel_irqfd_resampler {
	struct kvm *kvm;
	/*
	 * List of resampling struct _irqfd objects sharing this gsi.
	 * RCU list modified under kvm->irqfds.resampler_lock
	 */
	struct list_head list;
	struct kvm_irq_ack_notifier notifier;
	/*
	 * Entry in list of kvm->irqfd.resampler_list.  Use for sharing
	 * resamplers among irqfds on the same gsi.
	 * RCU list modified under kvm->irqfds.resampler_lock
	 */
	struct list_head link;
};

struct kvm_kernel_irqfd {
	/* Used for MSI fast-path */
	struct kvm *kvm;
	wait_queue_entry_t wait;
	/* Update side is protected by irqfds.lock */
	struct kvm_kernel_irq_routing_entry irq_entry;
	seqcount_spinlock_t irq_entry_sc;
	/* Used for level IRQ fast-path */
	int gsi;
	struct work_struct inject;
	/* The resampler used by this irqfd (resampler-only) */
	struct kvm_kernel_irqfd_resampler *resampler;
	/* Eventfd notified on resample (resampler-only) */
	struct eventfd_ctx *resamplefd;
	/* Entry in list of irqfds for a resampler (resampler-only) */
	struct list_head resampler_link;
	/* Used for setup/shutdown */
	struct eventfd_ctx *eventfd;
	struct list_head list;
	struct work_struct shutdown;
	struct irq_bypass_consumer consumer;
	struct irq_bypass_producer *producer;

	struct kvm_vcpu *irq_bypass_vcpu;
	struct list_head vcpu_list;
	void *irq_bypass_data;
};

#endif /* __LINUX_KVM_IRQFD_H */
