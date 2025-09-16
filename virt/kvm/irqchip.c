// SPDX-License-Identifier: GPL-2.0-only
/*
 * irqchip.c: Common API for in kernel interrupt controllers
 * Copyright (c) 2007, Intel Corporation.
 * Copyright 2010 Red Hat, Inc. and/or its affiliates.
 * Copyright (c) 2013, Alexander Graf <agraf@suse.de>
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

int kvm_irq_map_gsi(struct kvm *kvm,
		    struct kvm_kernel_irq_routing_entry *entries, int gsi)
{
	struct kvm_irq_routing_table *irq_rt;
	struct kvm_kernel_irq_routing_entry *e;
	int n = 0;

	irq_rt = srcu_dereference_check(kvm->irq_routing, &kvm->irq_srcu,
					lockdep_is_held(&kvm->irq_lock));
	if (irq_rt && gsi < irq_rt->nr_rt_entries) {
		hlist_for_each_entry(e, &irq_rt->map[gsi], link) {
			entries[n] = *e;
			++n;
		}
	}

	return n;
}

int kvm_irq_map_chip_pin(struct kvm *kvm, unsigned irqchip, unsigned pin)
{
	struct kvm_irq_routing_table *irq_rt;

	irq_rt = srcu_dereference(kvm->irq_routing, &kvm->irq_srcu);
	return irq_rt->chip[irqchip][pin];
}

int kvm_send_userspace_msi(struct kvm *kvm, struct kvm_msi *msi)
{
	struct kvm_kernel_irq_routing_entry route;

	if (!kvm_arch_irqchip_in_kernel(kvm) || (msi->flags & ~KVM_MSI_VALID_DEVID))
		return -EINVAL;

	route.msi.address_lo = msi->address_lo;
	route.msi.address_hi = msi->address_hi;
	route.msi.data = msi->data;
	route.msi.flags = msi->flags;
	route.msi.devid = msi->devid;

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
	struct kvm_kernel_irq_routing_entry irq_set[KVM_NR_IRQCHIPS];
	int ret = -1, i, idx;

	trace_kvm_set_irq(irq, level, irq_source_id);

	/* Not possible to detect if the guest uses the PIC or the
	 * IOAPIC.  So set the bit in both. The guest will ignore
	 * writes to the unused one.
	 */
	idx = srcu_read_lock(&kvm->irq_srcu);
	i = kvm_irq_map_gsi(kvm, irq_set, irq);
	srcu_read_unlock(&kvm->irq_srcu, idx);

	while (i--) {
		int r;
		r = irq_set[i].set(&irq_set[i], kvm, irq_source_id, level,
				   line_status);
		if (r < 0)
			continue;

		ret = r + ((ret < 0) ? 0 : ret);
	}

	return ret;
}

static void free_irq_routing_table(struct kvm_irq_routing_table *rt)
{
	int i;

	if (!rt)
		return;

	for (i = 0; i < rt->nr_rt_entries; ++i) {
		struct kvm_kernel_irq_routing_entry *e;
		struct hlist_node *n;

		hlist_for_each_entry_safe(e, n, &rt->map[i], link) {
			hlist_del(&e->link);
			kfree(e);
		}
	}

	kfree(rt);
}

void kvm_free_irq_routing(struct kvm *kvm)
{
	/* Called only during vm destruction. Nobody can use the pointer
	   at this stage */
	struct kvm_irq_routing_table *rt = rcu_access_pointer(kvm->irq_routing);
	free_irq_routing_table(rt);
}

static int setup_routing_entry(struct kvm *kvm,
			       struct kvm_irq_routing_table *rt,
			       struct kvm_kernel_irq_routing_entry *e,
			       const struct kvm_irq_routing_entry *ue)
{
	struct kvm_kernel_irq_routing_entry *ei;
	int r;
	u32 gsi = array_index_nospec(ue->gsi, KVM_MAX_IRQ_ROUTES);

	/*
	 * Do not allow GSI to be mapped to the same irqchip more than once.
	 * Allow only one to one mapping between GSI and non-irqchip routing.
	 */
	hlist_for_each_entry(ei, &rt->map[gsi], link)
		if (ei->type != KVM_IRQ_ROUTING_IRQCHIP ||
		    ue->type != KVM_IRQ_ROUTING_IRQCHIP ||
		    ue->u.irqchip.irqchip == ei->irqchip.irqchip)
			return -EINVAL;

	e->gsi = gsi;
	e->type = ue->type;
	r = kvm_set_routing_entry(kvm, e, ue);
	if (r)
		return r;
	if (e->type == KVM_IRQ_ROUTING_IRQCHIP)
		rt->chip[e->irqchip.irqchip][e->irqchip.pin] = e->gsi;

	hlist_add_head(&e->link, &rt->map[e->gsi]);

	return 0;
}

void __attribute__((weak)) kvm_arch_irq_routing_update(struct kvm *kvm)
{
}

bool __weak kvm_arch_can_set_irq_routing(struct kvm *kvm)
{
	return true;
}

int kvm_set_irq_routing(struct kvm *kvm,
			const struct kvm_irq_routing_entry *ue,
			unsigned nr,
			unsigned flags)
{
	struct kvm_irq_routing_table *new, *old;
	struct kvm_kernel_irq_routing_entry *e;
	u32 i, j, nr_rt_entries = 0;
	int r;

	for (i = 0; i < nr; ++i) {
		if (ue[i].gsi >= KVM_MAX_IRQ_ROUTES)
			return -EINVAL;
		nr_rt_entries = max(nr_rt_entries, ue[i].gsi);
	}

	nr_rt_entries += 1;

	new = kzalloc(struct_size(new, map, nr_rt_entries), GFP_KERNEL_ACCOUNT);
	if (!new)
		return -ENOMEM;

	new->nr_rt_entries = nr_rt_entries;
	for (i = 0; i < KVM_NR_IRQCHIPS; i++)
		for (j = 0; j < KVM_IRQCHIP_NUM_PINS; j++)
			new->chip[i][j] = -1;

	for (i = 0; i < nr; ++i) {
		r = -ENOMEM;
		e = kzalloc(sizeof(*e), GFP_KERNEL_ACCOUNT);
		if (!e)
			goto out;

		r = -EINVAL;
		switch (ue->type) {
		case KVM_IRQ_ROUTING_MSI:
			if (ue->flags & ~KVM_MSI_VALID_DEVID)
				goto free_entry;
			break;
		default:
			if (ue->flags)
				goto free_entry;
			break;
		}
		r = setup_routing_entry(kvm, new, e, ue);
		if (r)
			goto free_entry;
		++ue;
	}

	mutex_lock(&kvm->irq_lock);
	old = rcu_dereference_protected(kvm->irq_routing, 1);
	rcu_assign_pointer(kvm->irq_routing, new);
	kvm_irq_routing_update(kvm);
	kvm_arch_irq_routing_update(kvm);
	mutex_unlock(&kvm->irq_lock);

	synchronize_srcu_expedited(&kvm->irq_srcu);

	new = old;
	r = 0;
	goto out;

free_entry:
	kfree(e);
out:
	free_irq_routing_table(new);

	return r;
}

/*
 * Allocate empty IRQ routing by default so that additional setup isn't needed
 * when userspace-driven IRQ routing is activated, and so that kvm->irq_routing
 * is guaranteed to be non-NULL.
 */
int kvm_init_irq_routing(struct kvm *kvm)
{
	struct kvm_irq_routing_table *new;
	int chip_size;

	new = kzalloc(struct_size(new, map, 1), GFP_KERNEL_ACCOUNT);
	if (!new)
		return -ENOMEM;

	new->nr_rt_entries = 1;

	chip_size = sizeof(int) * KVM_NR_IRQCHIPS * KVM_IRQCHIP_NUM_PINS;
	memset(new->chip, -1, chip_size);

	RCU_INIT_POINTER(kvm->irq_routing, new);

	return 0;
}
