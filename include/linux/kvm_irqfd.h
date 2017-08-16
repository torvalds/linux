/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
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
	 * Accessed and modified under kvm->irqfds.resampler_lock
	 */
	struct list_head link;
};

struct kvm_kernel_irqfd {
	/* Used for MSI fast-path */
	struct kvm *kvm;
	wait_queue_entry_t wait;
	/* Update side is protected by irqfds.lock */
	struct kvm_kernel_irq_routing_entry irq_entry;
	seqcount_t irq_entry_sc;
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
	poll_table pt;
	struct work_struct shutdown;
	struct irq_bypass_consumer consumer;
	struct irq_bypass_producer *producer;
};

#endif /* __LINUX_KVM_IRQFD_H */
