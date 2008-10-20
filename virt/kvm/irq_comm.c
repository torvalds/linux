/*
 * irq_comm.c: Common API for in kernel interrupt controller
 * Copyright (c) 2007, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 * Authors:
 *   Yaozu (Eddie) Dong <Eddie.dong@intel.com>
 *
 */

#include <linux/kvm_host.h>
#include "irq.h"

#include "ioapic.h"

/* This should be called with the kvm->lock mutex held */
void kvm_set_irq(struct kvm *kvm, int irq, int level)
{
	/* Not possible to detect if the guest uses the PIC or the
	 * IOAPIC.  So set the bit in both. The guest will ignore
	 * writes to the unused one.
	 */
	kvm_ioapic_set_irq(kvm->arch.vioapic, irq, level);
#ifdef CONFIG_X86
	kvm_pic_set_irq(pic_irqchip(kvm), irq, level);
#endif
}

void kvm_notify_acked_irq(struct kvm *kvm, unsigned gsi)
{
	struct kvm_irq_ack_notifier *kian;
	struct hlist_node *n;

	hlist_for_each_entry(kian, n, &kvm->arch.irq_ack_notifier_list, link)
		if (kian->gsi == gsi)
			kian->irq_acked(kian);
}

void kvm_register_irq_ack_notifier(struct kvm *kvm,
				   struct kvm_irq_ack_notifier *kian)
{
	hlist_add_head(&kian->link, &kvm->arch.irq_ack_notifier_list);
}

void kvm_unregister_irq_ack_notifier(struct kvm *kvm,
				     struct kvm_irq_ack_notifier *kian)
{
	hlist_del(&kian->link);
}
