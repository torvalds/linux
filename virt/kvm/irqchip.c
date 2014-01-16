/*
 * irqchip.c: Common API for in kernel interrupt controllers
 * Copyright (c) 2007, Intel Corporation.
 * Copyright 2010 Red Hat, Inc. and/or its affiliates.
 * Copyright (c) 2013, Alexander Graf <agraf@suse.de>
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
 *
 * This file is derived from virt/kvm/irq_comm.c.
 *
 * Authors:
 *   Yaozu (Eddie) Dong <Eddie.dong@intel.com>
 *   Alexander Graf <agraf@suse.de>
 */

#include <linux/kvm_host.h>
#include <linux/slab.h>
#include <linux/srcu.h>
#include <linux/export.h>
#include <trace/events/kvm.h>
#include "irq.h"

bool kvm_irq_has_notifier(struct kvm *kvm, unsigned irqchip, unsigned pin)
{
	struct kvm_irq_ack_notifier *kian;
	int gsi, idx;

	idx = srcu_read_lock(&kvm->irq_srcu);
	gsi = srcu_dereference(kvm->irq_routing, &kvm->irq_srcu)->chip[irqchip][pin];
	if (gsi != -1)
		hlist_for_each_entry_rcu(kian, &kvm->irq_ack_notifier_list,
					 link)
			if (kian->gsi == gsi) {
				srcu_read_unlock(&kvm->irq_srcu, idx);
				return true;
			}

	srcu_read_unlock(&kvm->irq_srcu, idx);

	return false;
}
EXPORT_SYMBOL_GPL(kvm_irq_has_notifier);

void kvm_notify_acked_irq(struct kvm *kvm, unsigned irqchip, unsigned pin)
{
	struct kvm_irq_ack_notifier *kian;
	int gsi, idx;

	trace_kvm_ack_irq(irqchip, pin);

	idx = srcu_read_lock(&kvm->irq_srcu);
	gsi = srcu_dereference(kvm->irq_routing, &kvm->irq_srcu)->chip[irqchip][pin];
	if (gsi != -1)
		hlist_for_each_entry_rcu(kian, &kvm->irq_ack_notifier_list,
					 link)
			if (kian->gsi == gsi)
				kian->irq_acked(kian);
	srcu_read_unlock(&kvm->irq_srcu, idx);
}

void kvm_register_irq_ack_notifier(struct kvm *kvm,
				   struct kvm_irq_ack_notifier *kian)
{
	mutex_lock(&kvm->irq_lock);
	hlist_add_head_rcu(&kian->link, &kvm->irq_ack_notifier_list);
	mutex_unlock(&kvm->irq_lock);
#ifdef __KVM_HAVE_IOAPIC
	kvm_vcpu_request_scan_ioapic(kvm);
#endif
}

void kvm_unregister_irq_ack_notifier(struct kvm *kvm,
				    struct kvm_irq_ack_notifier *kian)
{
	mutex_lock(&kvm->irq_lock);
	hlist_del_init_rcu(&kian->link);
	mutex_unlock(&kvm->irq_lock);
	synchronize_srcu(&kvm->irq_srcu);
#ifdef __KVM_HAVE_IOAPIC
	kvm_vcpu_request_scan_ioapic(kvm);
#endif
}

int kvm_send_userspace_msi(struct kvm *kvm, struct kvm_msi *msi)
{
	struct kvm_kernel_irq_routing_entry route;

	if (!irqchip_in_kernel(kvm) || msi->flags != 0)
		return -EINVAL;

	route.msi.address_lo = msi->address_lo;
	route.msi.address_hi = msi->address_hi;
	route.msi.data = msi->data;

	return kvm_set_msi(&route, kvm, KVM_USERSPACE_IRQ_SOURCE_ID, 1, false);
}

/*
 * Return value:
 *  < 0   Interrupt was ignored (masked or not delivered for other reasons)
 *  = 0   Interrupt was coalesced (previous irq is still pending)
 *  > 0   Number of CPUs interrupt was delivered to
 */
int kvm_set_irq(struct kvm *kvm, int irq_source_id, u32 irq, int level,
		bool line_status)
{
	struct kvm_kernel_irq_routing_entry *e, irq_set[KVM_NR_IRQCHIPS];
	int ret = -1, i = 0, idx;
	struct kvm_irq_routing_table *irq_rt;

	trace_kvm_set_irq(irq, level, irq_source_id);

	/* Not possible to detect if the guest uses the PIC or the
	 * IOAPIC.  So set the bit in both. The guest will ignore
	 * writes to the unused one.
	 */
	idx = srcu_read_lock(&kvm->irq_srcu);
	irq_rt = srcu_dereference(kvm->irq_routing, &kvm->irq_srcu);
	if (irq < irq_rt->nr_rt_entries)
		hlist_for_each_entry(e, &irq_rt->map[irq], link)
			irq_set[i++] = *e;
	srcu_read_unlock(&kvm->irq_srcu, idx);

	while(i--) {
		int r;
		r = irq_set[i].set(&irq_set[i], kvm, irq_source_id, level,
				   line_status);
		if (r < 0)
			continue;

		ret = r + ((ret < 0) ? 0 : ret);
	}

	return ret;
}

void kvm_free_irq_routing(struct kvm *kvm)
{
	/* Called only during vm destruction. Nobody can use the pointer
	   at this stage */
	kfree(kvm->irq_routing);
}

static int setup_routing_entry(struct kvm_irq_routing_table *rt,
			       struct kvm_kernel_irq_routing_entry *e,
			       const struct kvm_irq_routing_entry *ue)
{
	int r = -EINVAL;
	struct kvm_kernel_irq_routing_entry *ei;

	/*
	 * Do not allow GSI to be mapped to the same irqchip more than once.
	 * Allow only one to one mapping between GSI and MSI.
	 */
	hlist_for_each_entry(ei, &rt->map[ue->gsi], link)
		if (ei->type == KVM_IRQ_ROUTING_MSI ||
		    ue->type == KVM_IRQ_ROUTING_MSI ||
		    ue->u.irqchip.irqchip == ei->irqchip.irqchip)
			return r;

	e->gsi = ue->gsi;
	e->type = ue->type;
	r = kvm_set_routing_entry(rt, e, ue);
	if (r)
		goto out;

	hlist_add_head(&e->link, &rt->map[e->gsi]);
	r = 0;
out:
	return r;
}

int kvm_set_irq_routing(struct kvm *kvm,
			const struct kvm_irq_routing_entry *ue,
			unsigned nr,
			unsigned flags)
{
	struct kvm_irq_routing_table *new, *old;
	u32 i, j, nr_rt_entries = 0;
	int r;

	for (i = 0; i < nr; ++i) {
		if (ue[i].gsi >= KVM_MAX_IRQ_ROUTES)
			return -EINVAL;
		nr_rt_entries = max(nr_rt_entries, ue[i].gsi);
	}

	nr_rt_entries += 1;

	new = kzalloc(sizeof(*new) + (nr_rt_entries * sizeof(struct hlist_head))
		      + (nr * sizeof(struct kvm_kernel_irq_routing_entry)),
		      GFP_KERNEL);

	if (!new)
		return -ENOMEM;

	new->rt_entries = (void *)&new->map[nr_rt_entries];

	new->nr_rt_entries = nr_rt_entries;
	for (i = 0; i < KVM_NR_IRQCHIPS; i++)
		for (j = 0; j < KVM_IRQCHIP_NUM_PINS; j++)
			new->chip[i][j] = -1;

	for (i = 0; i < nr; ++i) {
		r = -EINVAL;
		if (ue->flags)
			goto out;
		r = setup_routing_entry(new, &new->rt_entries[i], ue);
		if (r)
			goto out;
		++ue;
	}

	mutex_lock(&kvm->irq_lock);
	old = kvm->irq_routing;
	kvm_irq_routing_update(kvm, new);
	mutex_unlock(&kvm->irq_lock);

	synchronize_srcu_expedited(&kvm->irq_srcu);

	new = old;
	r = 0;

out:
	kfree(new);
	return r;
}
